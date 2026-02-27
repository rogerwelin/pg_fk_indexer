#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(pg_fk_indexer_add);

Datum pg_fk_indexer_add(PG_FUNCTION_ARGS) {
    int32 a = PG_GETARG_INT32(0);
    int32 b = PG_GETARG_INT32(1);

    PG_RETURN_INT32(a + b);
}
