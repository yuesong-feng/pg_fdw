/* contrib/pg_fdw--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION pg_fdw" to load this file. \quit

CREATE FUNCTION pg_fdw_handler()
RETURNS fdw_handler
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FUNCTION pg_fdw_validator(text[], oid)
RETURNS void
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT;

CREATE FOREIGN DATA WRAPPER pg_fdw
HANDLER pg_fdw_handler
VALIDATOR pg_fdw_validator;

CREATE SERVER fdw_server FOREIGN DATA WRAPPER pg_fdw;
CREATE FOREIGN TABLE test1 (id int) SERVER fdw_server;

