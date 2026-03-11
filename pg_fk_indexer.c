#include "postgres.h"
#include "fmgr.h"

/* access — low-level heap/index scanning and table access */
#include "access/attnum.h"
#include "access/genam.h"
#include "access/htup_details.h"
#include "access/relation.h"
#include "access/stratnum.h"
#include "access/table.h"

/* executor — SPI (Server Programming Interface) for running SQL from C */
#include "executor/spi.h"

/* lib — string buffer utilities */
#include "lib/stringinfo.h"

/* catalog — system catalog lookups (pg_constraint, pg_index, namespace resolution) */
#include "catalog/namespace.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_index.h"

/* utils — function OID constants (F_OIDEQ, etc.) */
#include "utils/fmgroids.h"

/* nodes — parse tree types and linked list utilities */
#include "nodes/nodes.h"
#include "nodes/parsenodes.h"
#include "nodes/pg_list.h"

/* tcop — utility statement hook infrastructure */
#include "tcop/utility.h"

/* utils — arrays, caching, relation metadata, logging */
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/elog.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"

PG_MODULE_MAGIC;

static ProcessUtility_hook_type prev_utility_hook = NULL;
static bool pg_fk_indexer_enabled = true;

static void
inject_index(RangeVar *relation, char *colName)
{
        StringInfoData buf;
        int                     ret;

        initStringInfo(&buf);

        /*
         * CREATE INDEX IF NOT EXISTS <index_name> ON <schema>.<table> (<column>)
         * Use quote_identifier to handle names with spaces, reserved words, or
         * mixed case.
         */
        appendStringInfo(&buf, "CREATE INDEX IF NOT EXISTS %s_%s_idx ON %s%s%s (%s)",
                          quote_identifier(relation->relname),
                          quote_identifier(colName),
                          (relation->schemaname ? quote_identifier(relation->schemaname) : ""),
                          (relation->schemaname ? "." : ""),
                          quote_identifier(relation->relname),
                          quote_identifier(colName));

        /* elog(NOTICE, "pg_fk_indexer: Auto-indexing: %s", buf.data); */

        if ((ret = SPI_connect()) != SPI_OK_CONNECT)
                elog(ERROR, "pg_fk_indexer: SPI_connect failed with error %d", ret);

        ret = SPI_execute(buf.data, false, 0);

        if (ret != SPI_OK_UTILITY)
                elog(ERROR, "pg_fk_indexer: SPI_execute failed with error %d", ret);

        SPI_finish();
        pfree(buf.data);
}


static bool
is_column_indexed(Oid relid, AttrNumber attnum)
{
        Relation        rel;
        List       *indexlist;
        ListCell   *lc;
        bool            found = false;

        /* open table with light lock */
        rel = relation_open(relid, AccessShareLock);

        /* get oid of all indexes on this table */
        indexlist = RelationGetIndexList(rel);

        foreach(lc, indexlist)
        {
                Oid                     indexOid = lfirst_oid(lc);
                HeapTuple       indexTuple;
                Form_pg_index indexForm;

                /* Look up the specific index in the pg_index catalog */
                indexTuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(indexOid));
                if (!HeapTupleIsValid(indexTuple))
                {
                        continue;
                }

                indexForm = (Form_pg_index) GETSTRUCT(indexTuple);

                /*
                 * Check if our column is the LEADING column (index 0). Postgres
                 * represents the array of columns in indkey.
                 */
                if (indexForm->indkey.values[0] == attnum)
                {
                        found = true;
                        ReleaseSysCache(indexTuple);
                        break;
                }
                ReleaseSysCache(indexTuple);
        }

        /* 3. Close the relation and return our findings */
        relation_close(rel, AccessShareLock);

        return found;
}

static void
analyze_table_fks(Oid relid, RangeVar *relation)
{
        Relation        pg_constraint_rel;
        SysScanDesc scan;
        ScanKeyData skey;
        HeapTuple       tuple;

        pg_constraint_rel = table_open(ConstraintRelationId, AccessShareLock);

        ScanKeyInit(&skey,
                    Anum_pg_constraint_conrelid,
                    BTEqualStrategyNumber,
                    F_OIDEQ,
                    ObjectIdGetDatum(relid));

        scan = systable_beginscan(pg_constraint_rel,
                                                  ConstraintRelidTypidNameIndexId,
                                                  true, NULL, 1, &skey);

        while (HeapTupleIsValid(tuple = systable_getnext(scan)))
        {
                Form_pg_constraint con = (Form_pg_constraint) GETSTRUCT(tuple);

                if (con->contype == CONSTRAINT_FOREIGN)
                {
                        bool            isNull;
                        Datum           adatum;

                        ArrayType  *arr;
                        int16      *attnums;
                        AttrNumber      attnum;
                        char       *colName;

                        adatum = heap_getattr(tuple,
                                              Anum_pg_constraint_conkey,
                                              RelationGetDescr(pg_constraint_rel),
                                              &isNull);

                        if (!isNull)
                        {
                                arr = DatumGetArrayTypeP(adatum);
                                attnums = (int16 *) ARR_DATA_PTR(arr);

                                /* Now we can assign values safely */
                                attnum = attnums[0];
                                colName = get_attname(relid, attnum, false);

                                if (!is_column_indexed(relid, attnum))
                                {
                                        /*
                                         * elog(NOTICE, "pg_fk_indexer: %s.%s NEEDS an index!",
                                         * relation->relname, colName);
                                         */
                                        inject_index(relation, colName);
                                }

                                if (colName)
                                {
                                        pfree(colName);
                                }
                        }
                }
        }

        systable_endscan(scan);
        table_close(pg_constraint_rel, AccessShareLock);
}

/*  Postgres 14+, check this */
static void
pg_fk_indexer_utility_hook(PlannedStmt *pstmt, const char *queryString,
                           bool readOnlyTree, ProcessUtilityContext context,
                           ParamListInfo params, QueryEnvironment *queryEnv,
                           DestReceiver *dest, QueryCompletion *qc)
{
        Node       *parsetree = pstmt->utilityStmt;

        if (prev_utility_hook)
        {
                prev_utility_hook(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
        }
        else
        {
                standard_ProcessUtility(pstmt, queryString, readOnlyTree, context, params, queryEnv, dest, qc);
        }

        /* Only act on the user's primary command */
        if (context == PROCESS_UTILITY_TOPLEVEL && pg_fk_indexer_enabled)
        {
                RangeVar   *rv = NULL;

                if (IsA(parsetree, CreateStmt))
                {
                        rv = ((CreateStmt *) parsetree)->relation;
                }
                else if (IsA(parsetree, AlterTableStmt))
                {
                        rv = ((AlterTableStmt *) parsetree)->relation;
                }

                if (rv)
                {
                        /* Table exists now, get its OID */
                        Oid                     relid = RangeVarGetRelid(rv, NoLock, true);

                        if (OidIsValid(relid))
                        {
                                analyze_table_fks(relid, rv);
                        }
                }
        }
}

/*  runs when extention is loaded into memory */
void
_PG_init(void)
{
        DefineCustomBoolVariable("pg_fk_indexer.enabled",
                                  "Automatically create indexes on foreign key columns",
                                  NULL,
                                  &pg_fk_indexer_enabled,
                                  true,
                                  PGC_USERSET,
                                  0,
                                  NULL,
                                  NULL,
                                  NULL);

        prev_utility_hook = ProcessUtility_hook;

        ProcessUtility_hook = pg_fk_indexer_utility_hook;
}

/*  runs when extention is unloaded */
void
_PG_fini(void)
{
        ProcessUtility_hook = prev_utility_hook;
}
