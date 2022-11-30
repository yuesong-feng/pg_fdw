#include "postgres.h"
#include "fmgr.h"
#include "foreign/fdwapi.h"
#include "optimizer/pathnode.h"
#include "optimizer/restrictinfo.h"
#include "optimizer/planmain.h"

PG_MODULE_MAGIC;

/*
 * FDW-specific information for RelOptInfo.fdw_private.
 */
typedef struct FdwPlanState
{
} FdwPlanState;

/*
 * FDW-specific information for ForeignScanState.fdw_state.
 */
typedef struct FdwExecutionState
{
} FdwExecutionState;

typedef struct FdwModifyState
{

} FdwModifyState;

/*
 * FDW callback routines
 */

static void fdwGetForeignRelSize(PlannerInfo *root,
                                 RelOptInfo *baserel,
                                 Oid foreigntableid);
static void fdwGetForeignPaths(PlannerInfo *root,
                               RelOptInfo *baserel,
                               Oid foreigntableid);
static ForeignScan *fdwGetForeignPlan(PlannerInfo *root,
                                      RelOptInfo *baserel,
                                      Oid foreigntableid,
                                      ForeignPath *best_path,
                                      List *tlist,
                                      List *scan_clauses,
                                      Plan *outer_plan);
static void fdwBeginForeignScan(ForeignScanState *node, int eflags);
static TupleTableSlot *fdwIterateForeignScan(ForeignScanState *node);
static void fdwReScanForeignScan(ForeignScanState *node);
static void fdwEndForeignScan(ForeignScanState *node);

static List *fdwPlanForeignModify(PlannerInfo *root,
                                  ModifyTable *plan,
                                  Index resultRelation,
                                  int subplan_index);
static void fdwBeginForeignModify(ModifyTableState *mtstate,
                                  ResultRelInfo *rinfo,
                                  List *fdw_private,
                                  int subplan_index,
                                  int eflags);
static TupleTableSlot *fdwExecForeignInsert(EState *estate,
                                            ResultRelInfo *resultRelInfo,
                                            TupleTableSlot *slot,
                                            TupleTableSlot *planSlot);
static void fdwEndForeignModify(EState *estate,
                                ResultRelInfo *resultRelInfo);
/*
 * Helper functions
 */

/*
 * Foreign-data wrapper handler function: return a struct with pointers
 * to my callback routines.
 */
PG_FUNCTION_INFO_V1(pg_fdw_handler);
Datum pg_fdw_handler(PG_FUNCTION_ARGS)
{
    FdwRoutine *fdw_routine = makeNode(FdwRoutine);
    /* Functions for scanning foreign tables */
    fdw_routine->GetForeignRelSize = fdwGetForeignRelSize;
    fdw_routine->GetForeignPaths = fdwGetForeignPaths;
    fdw_routine->GetForeignPlan = fdwGetForeignPlan;
    fdw_routine->BeginForeignScan = fdwBeginForeignScan;
    fdw_routine->IterateForeignScan = fdwIterateForeignScan;
    fdw_routine->ReScanForeignScan = fdwReScanForeignScan;
    fdw_routine->EndForeignScan = fdwEndForeignScan;
    /* Functions for updating foreign tables */
    fdw_routine->PlanForeignModify = fdwPlanForeignModify;
    fdw_routine->BeginForeignModify = fdwBeginForeignModify;
    fdw_routine->ExecForeignInsert = fdwExecForeignInsert;
    fdw_routine->EndForeignModify = fdwEndForeignModify;
    PG_RETURN_POINTER(fdw_routine);
}

/*
 * fdwGetForeignRelSize
 *		Estimate # of rows and width of the result of the scan
 *
 * We should consider the effect of all baserestrictinfo clauses here, but
 * not any join clauses.
 */
static void fdwGetForeignRelSize(PlannerInfo *root, RelOptInfo *baserel,
                                 Oid foreignTableId)
{
    FdwPlanState *fdw_private;
    fdw_private = (FdwPlanState *)palloc0(sizeof(FdwPlanState));
    baserel->fdw_private = (void *)fdw_private;

    baserel->rows = 1;
}

/*
 * postgresGetForeignPaths
 *		Create possible scan paths for a scan on the foreign table
 */
static void fdwGetForeignPaths(PlannerInfo *root,
                               RelOptInfo *baserel,
                               Oid foreigntableid)
{
    FdwPlanState *fdw_private = (FdwPlanState *)baserel->fdw_private;

    Cost startup_cost = 0;
    Cost total_cost = 0;
    add_path(baserel, (Path *)create_foreignscan_path(root, baserel,
                                                      NULL, /* default pathtarget */
                                                      baserel->rows,
                                                      startup_cost,
                                                      total_cost,
                                                      NIL,   /* no pathkeys */
                                                      NULL,  /* no outer rel either */
                                                      NULL,  /* no extra plan */
                                                      NIL)); /* no fdw_private list */
}

/*
 * postgresGetForeignPlan
 *		Create ForeignScan plan node which implements selected best path
 */
static ForeignScan *fdwGetForeignPlan(PlannerInfo *root,
                                      RelOptInfo *baserel,
                                      Oid foreigntableid,
                                      ForeignPath *best_path,
                                      List *tlist,
                                      List *scan_clauses,
                                      Plan *outer_plan)
{
    Index scan_relid = baserel->relid;

    /*
     * We have no native ability to evaluate restriction clauses, so we just
     * put all the scan_clauses into the plan node's qual list for the
     * executor to check.  So all we have to do here is strip RestrictInfo
     * nodes from the clauses and ignore pseudoconstants (which will be
     * handled elsewhere).
     */
    scan_clauses = extract_actual_clauses(scan_clauses, false);

    /* Create the ForeignScan node */
    return make_foreignscan(tlist,
                            scan_clauses,
                            scan_relid,
                            NIL, /* no expressions to evaluate */
                            NIL, /* no private state either */
                            NIL, /* no custom tlist */
                            NIL, /* no remote quals */
                            outer_plan);
}

/*
 * postgresBeginForeignScan
 *		Initiate an executor scan of a foreign table.
 */
static void fdwBeginForeignScan(ForeignScanState *node, int eflags)
{
    ForeignScan *fsplan = (ForeignScan *)node->ss.ps.plan;

    FdwExecutionState *festate = (FdwExecutionState *)palloc0(sizeof(FdwExecutionState));
    node->fdw_state = (void *)festate;
}

/*
 * postgresIterateForeignScan
 *		Retrieve next row from the result set, or clear tuple slot to indicate
 *		EOF.
 */
static TupleTableSlot *fdwIterateForeignScan(ForeignScanState *node)
{
    FdwExecutionState *festate = (FdwExecutionState *)node->fdw_state;
    TupleTableSlot *slot = node->ss.ss_ScanTupleSlot;
    // TODO
    return slot;
}

/*
 * postgresReScanForeignScan
 *		Restart the scan.
 */
static void fdwReScanForeignScan(ForeignScanState *node)
{
    FdwExecutionState *festate = (FdwExecutionState *)node->fdw_state;
}

/*
 * postgresEndForeignScan
 *		Finish scanning foreign table and dispose objects used for this scan
 */
static void fdwEndForeignScan(ForeignScanState *node)
{
    FdwExecutionState *festate = (FdwExecutionState *)node->fdw_state;
}

////////////////////////////

/*
 * fdwPlanForeignModify
 *		Plan an insert/update/delete operation on a foreign table
 */
static List *fdwPlanForeignModify(PlannerInfo *root,
                                  ModifyTable *plan,
                                  Index resultRelation,
                                  int subplan_index)
{
    CmdType operation = plan->operation;
    switch (operation)
    {
    case CMD_INSERT:

        break;

    default:
        ereport(ERROR, (errcode(ERRCODE_FDW_INVALID_HANDLE), errmsg("not suported: %d", (int)operation)));
        break;
    }
    return NIL;
}

/*
 * fdwBeginForeignModify
 *		Begin an insert/update/delete operation on a foreign table
 */
static void fdwBeginForeignModify(ModifyTableState *mtstate,
                                  ResultRelInfo *rinfo,
                                  List *fdw_private,
                                  int subplan_index,
                                  int eflags)
{
    FdwModifyState *fmstate = (FdwModifyState *)palloc0(sizeof(FdwModifyState));
    rinfo->ri_FdwState = (void *)fmstate;
}

/*
 * fdwExecForeignInsert
 *		Insert one row into a foreign table
 */
static TupleTableSlot *fdwExecForeignInsert(EState *estate,
                                            ResultRelInfo *rinfo,
                                            TupleTableSlot *slot,
                                            TupleTableSlot *planSlot)
{
    FdwModifyState *fmstate = (FdwModifyState *)rinfo->ri_FdwState;
    return slot;
}

/*
 * fdwEndForeignModify
 *		Finish an insert/update/delete operation on a foreign table
 */
static void fdwEndForeignModify(EState *estate,
                                ResultRelInfo *rinfo)
{
    FdwModifyState *fmstate = (FdwModifyState *)rinfo->ri_FdwState;
}