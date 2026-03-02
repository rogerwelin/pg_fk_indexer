#include "postgres.h"
#include "fmgr.h"
#include "utils/builtins.h"
#include "tcop/utility.h" // utility hook
#include "nodes/nodes.h"
#include "nodes/pg_list.h"
#include "nodes/parsenodes.h"
#include "utils/elog.h"

PG_MODULE_MAGIC;


static ProcessUtility_hook_type prev_utility_hook = NULL;


// Postgres 14+, check this
static void pg_fk_indexer_utility_hook(PlannedStmt *pstmt, const char *queryString,
                                  bool readOnlyTree, ProcessUtilityContext context,
                                  ParamListInfo params, QueryEnvironment *queryEnv,
                                  DestReceiver *dest, QueryCompletion *qc) {
  Node *parsetree = pstmt->utilityStmt;

  if (nodeTag(parsetree) == T_CreateStmt) {
    CreateStmt *stmt = (CreateStmt *) parsetree;
    char *tableName = stmt->relation->relname;
    elog(NOTICE, "pg_fk_indexer: creating table: '%s'", tableName);

    ListCell *cell;

    // loop table elements (columns and constraints)
    foreach(cell, stmt->tableElts) {
      Node *element = (Node *) lfirst(cell);

      // table level constraints, eg CREATE TABLE t (a int, b int, FOREIGN KEY (a) REFERENCES other(x));
      // parser sees this as a standalone Constraint node in the list.
      if (IsA(element, Constraint))  {
        Constraint *con = (Constraint *) element;
        if (con->contype == CONSTR_FOREIGN) {
          /* Table-level FKs store column names in the 'fk_attrs' list */
          char *colName = strVal(linitial(con->fk_attrs));
          elog(NOTICE, "pg_fk_indexer: Found table-level FK on column '%s'", colName);
        }
      }

      // Inline Column Constraint, eg CREATE TABLE t (a int REFERENCES other(x));
      // parser sees a ColumnDef node; the FK is nested inside its 'constraints' list
      if (IsA(element, ColumnDef)) {
        ColumnDef *colDef = (ColumnDef *) element;
        ListCell *colCell;

        foreach(colCell, colDef->constraints) {
          Node *colElement = (Node *) lfirst(colCell);

          if (IsA(colElement, Constraint)) {
            Constraint *con = (Constraint *) colElement;
            if (con->contype == CONSTR_FOREIGN) {
              /* For inline FKs, the column name is the name of the parent ColumnDef */
              elog(NOTICE, "pg_fk_indexer: Found inline FK on column '%s'", colDef->colname);
            }
          }
        }
      }
    }
  }

  if (nodeTag(parsetree) == T_AlterTableStmt) {
    AlterTableStmt *stmt = (AlterTableStmt *) parsetree;
    char *tableName = stmt->relation->relname;
    elog(NOTICE, "pg_fk_indexer: altering table: '%s'", tableName);
  }


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
