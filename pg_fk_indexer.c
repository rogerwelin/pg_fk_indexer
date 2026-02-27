#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"

// postgres.h --> postgres core definitions; types, macros, server internals etc
// fmgr.h --> pulls in the "function manager" API. Datum type, PG_FUNCTION_ARGS macro, helpers
// utils/builtins.h --> helpers for working with built-in types like text, int4, etc


// Marks this shared library as a valid PostgreSQL loadable module. Postgres checks for ABI compability
PG_MODULE_MAGIC;

// Tells PostgreSQL about the function using the V1 calling convention
PG_FUNCTION_INFO_V1(pg_fk_indexer_hello);

Datum pg_fk_indexer_hello(PG_FUNCTION_ARGS) {
    PG_RETURN_TEXT_P(cstring_to_text("hello world from a postgres extension"));
}
