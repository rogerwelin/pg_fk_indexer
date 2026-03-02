#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "tcop/utility.h" // utility hook
#include "utils/elog.h"

PG_MODULE_MAGIC;


static ProcessUtility_hook_type prev_utility_hook = NULL;


// Postgres 14+, check this
static void pg_fk_indexer_utility_hook(PlannedStmt *pstmt, const char *queryString,
                                  bool readOnlyTree, ProcessUtilityContext context,
                                  ParamListInfo params, QueryEnvironment *queryEnv,
                                  DestReceiver *dest, QueryCompletion *qc) {
  elog(NOTICE, "pg_fk_indexer: loaded");

  // Chain to previous hook if one exists, otherwise call the default executor.
  // Skipping this would swallow the DDL and break other extensions in the hook chain.
  if (prev_utility_hook) {
    prev_utility_hook(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
  } else {
    standard_ProcessUtility(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
  }
}

// runs when extention is loaded into memory
void _PG_init(void) {
  prev_utility_hook = ProcessUtility_hook;

  ProcessUtility_hook = pg_fk_indexer_utility_hook;
}

// runs when extention is unloaded
void _PG_fini(void) {
  ProcessUtility_hook = prev_utility_hook;
}
