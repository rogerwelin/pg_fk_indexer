CREATE FUNCTION pg_fk_indexer_add(a int4, b int4)
RETURNS int4
AS 'MODULE_PATHNAME', 'pg_fk_indexer_add'
LANGUAGE C IMMUTABLE STRICT;
