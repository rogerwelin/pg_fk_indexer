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

static void extract_fks_from_create(CreateStmt *stmt) {
  // 2. In Postgres, a table's name and schema are stored in a struct called 'relation'
  char *tableName = stmt->relation->relname;
  ListCell *cell;
  elog(NOTICE, "pg_fk_indexer: Analyzing CREATE TABLE '%s'", tableName);

  // Iterate through the table elements (columns and constraints)
  foreach(cell, stmt->tableElts) {
    Node *element = (Node *) lfirst(cell);

    /* CASE A: Table-level Constraint
    * Example: CREATE TABLE t (a int, b int, FOREIGN KEY (a) REFERENCES other(x));
    * The parser sees this as a standalone Constraint node in the list.
    */
    if (IsA(element, Constraint))  {
      Constraint *con = (Constraint *) element;
      if (con->contype == CONSTR_FOREIGN) {
        /* Table-level FKs store column names in the 'fk_attrs' list */
        char *colName = strVal(linitial(con->fk_attrs));
        elog(NOTICE, "pg_fk_indexer: Found table-level FK on column '%s'", colName);
      }
    }

   /* CASE B: Inline Column Constraint
    * Example: CREATE TABLE t (a int REFERENCES other(x));
    * The parser sees a ColumnDef node; the FK is nested inside its 'constraints' list.
    */
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

static void extract_fks_from_alter(AlterTableStmt *stmt) {
  char *tableName = stmt->relation->relname;
  ListCell *cell;

  elog(NOTICE, "pg_fk_indexer: Analyzing ALTER TABLE '%s'", tableName);

  foreach(cell, stmt->cmds) {
    AlterTableCmd *cmd = (AlterTableCmd *) lfirst(cell);

    /* We only care about adding constraints */
    if (cmd->subtype == AT_AddConstraint && IsA(cmd->def, Constraint)) {
      Constraint *con = (Constraint *) cmd->def;

      if (con->contype == CONSTR_FOREIGN) {
        char *colName = strVal(linitial(con->fk_attrs));
        elog(NOTICE, "pg_fk_indexer: Found ALTER TABLE FK on column '%s'", colName);
      }
    }
  }
}

// Postgres 14+, check this
static void pg_fk_indexer_utility_hook(PlannedStmt *pstmt, const char *queryString,
                                  bool readOnlyTree, ProcessUtilityContext context,
                                  ParamListInfo params, QueryEnvironment *queryEnv,
                                  DestReceiver *dest, QueryCompletion *qc) {
  Node *parsetree = pstmt->utilityStmt;

  // Chain to previous hook if one exists, otherwise call the default executor.
  // Skipping this would swallow the DDL and break other extensions in the hook chain.
  if (prev_utility_hook) {
    prev_utility_hook(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
  } else {
    standard_ProcessUtility(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
  }

  if (nodeTag(parsetree) == T_CreateStmt) {
    extract_fks_from_create((CreateStmt *) parsetree);
  }

  if (nodeTag(parsetree) == T_AlterTableStmt) {
    extract_fks_from_alter((AlterTableStmt *) parsetree);
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
