# contrib/pg_fdw/Makefile

MODULE_big = pg_fdw
OBJS = pg_fdw.o option.o $(WIN32RES) 
PGFILEDESC = "pg_fdw - pg fdw example"

PG_CPPFLAGS = -I$(libpq_srcdir)
SHLIB_LINK_INTERNAL = $(libpq)

EXTENSION = pg_fdw
DATA = pg_fdw--1.0.sql

REGRESS = pg_fdw # sql、expected 文件夹下的测试，

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
SHLIB_PREREQS = submake-libpq
subdir = contrib/pg_fdw
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
