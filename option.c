#include "postgres.h"
#include "fmgr.h"
#include "catalog/pg_foreign_table.h"
#include "nodes/pg_list.h"
#include "nodes/parsenodes.h"
#include "lib/stringinfo.h"
#include "commands/defrem.h"
#include "access/reloptions.h"

/*
 * Describes the valid options for objects that this wrapper uses.
 */
typedef struct FdwOption
{
    const char *optname;
    Oid optcontext; /* Oid of catalog in which option may appear */
} FdwOption;

/*
 * Valid options.
 */
static const FdwOption valid_options[] = {
    {"db", ForeignTableRelationId},
    {"option2", ForeignTableRelationId},
    {"allow", ForeignTableRelationId},
    {NULL, InvalidOid}};

static bool is_valid_option(const char *option, Oid context);

PG_FUNCTION_INFO_V1(pg_fdw_validator);
/*
 * Validate the generic options given to a FOREIGN DATA WRAPPER, SERVER,
 * USER MAPPING or FOREIGN TABLE that uses file_fdw.
 *
 * Raise an ERROR if the option or its value is considered invalid.
 */
Datum pg_fdw_validator(PG_FUNCTION_ARGS)
{
    List *options_list = untransformRelOptions(PG_GETARG_DATUM(0));
    Oid catalog = PG_GETARG_OID(1);
    char *db = NULL;
    char *option2 = NULL;
    DefElem *allow = NULL;

    /*
     * Check that only options supported by pg_fdw, and allowed for the
     * current object type, are given.
     */
    ListCell *cell;
    foreach (cell, options_list)
    {
        DefElem *def = (DefElem *)lfirst(cell);
        if (!is_valid_option(def->defname, catalog))
        {
            /*
             * Unknown option specified, complain about it. Provide a hint
             * with list of valid options for the object.
             */
            const FdwOption *opt;
            StringInfoData buf;
            initStringInfo(&buf);
            for (opt = valid_options; opt->optname; opt++)
            {
                if (catalog == opt->optcontext)
                    appendStringInfo(&buf, "%s%s", (buf.len > 0) ? ", " : "", opt->optname);
            }
            ereport(ERROR,
                    (errcode(ERRCODE_FDW_INVALID_OPTION_NAME),
                     errmsg("invalid option \"%s\"", def->defname),
                     buf.len > 0
                         ? errhint("Valid options in this context are: %s", buf.data)
                         : errhint("There are no valid options in this context.")));
        }

        if (strcmp(def->defname, "db") == 0)
        {
            if (db)
                ereport(ERROR,
                        (errcode(ERRCODE_SYNTAX_ERROR),
                         errmsg("conflicting or redundant options")));
            db = defGetString(def);
        }
        else if (strcmp(def->defname, "option2") == 0)
        {
            if (option2)
                ereport(ERROR,
                        (errcode(ERRCODE_SYNTAX_ERROR),
                         errmsg("conflicting or redundant options")));
            option2 = defGetString(def);
        }
        else if (strcmp(def->defname, "allow") == 0)
        {
            if (allow)
                ereport(ERROR,
                        (errcode(ERRCODE_SYNTAX_ERROR),
                         errmsg("conflicting or redundant options")));
            allow = def;
            /* Don't care what the value is, as long as it's a legal boolean */
            (void)defGetBoolean(def);
        }

        /*
         * Option is required.
         */
        if (catalog == ForeignTableRelationId && db == NULL)
            ereport(ERROR,
                    (errcode(ERRCODE_FDW_DYNAMIC_PARAMETER_VALUE_NEEDED),
                     errmsg("db is required for file_fdw foreign tables")));
    }

    PG_RETURN_VOID();
}

/*
 * Check if the provided option is one of the valid options.
 * context is the Oid of the catalog holding the object the option is for.
 */
static bool
is_valid_option(const char *option, Oid context)
{
    const FdwOption *opt;
    for (opt = valid_options; opt->optname; opt++)
    {
        if (context == opt->optcontext && strcmp(opt->optname, option) == 0)
            return true;
    }
    return false;
}
