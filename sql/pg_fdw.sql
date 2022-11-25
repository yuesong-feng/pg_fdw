CREATE EXTENSION pg_fdw;

CREATE FOREIGN TABLE test2 (id int, name char(10))
SERVER fdw_server
OPTIONS (db 'dbname', option2 'option2');

CREATE FOREIGN TABLE test3 (id int, name char(10))
SERVER fdw_server
OPTIONS (db 'dbname', option2 'option2', allow 'allow');

CREATE FOREIGN TABLE test4 (id int, name char(10))
SERVER fdw_server
OPTIONS (option2 'option2', allow 'false');

CREATE FOREIGN TABLE test5 (id int, name char(10))
SERVER fdw_server
OPTIONS (db 'dbname', option2 'option2', allow 'true');
