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
static bool pg_fk_indexer_debug = false;

/*
 * simple_hash - djb2 hash 
 * Returns a 16-bit value used as a 4-hex-char suffix for truncated
 * index names.
 */
static uint32
simple_hash(const char *str)
{
        uint32 hash = 5381;

        while (*str)
                hash = ((hash << 5) + hash) + (unsigned char) *str++;

        return hash & 0xFFFF;
}

static void
inject_index(RangeVar *relation, char **colNames, int nCols)
{
        StringInfoData buf;
        StringInfoData namebuf;
        int                     ret;
        int                     i;
        const char     *idxname;

        /* Build the ideal index name: table_col1_col2_idx */
        initStringInfo(&namebuf);
        appendStringInfoString(&namebuf, relation->relname);
        for (i = 0; i < nCols; i++)
                appendStringInfo(&namebuf, "_%s", colNames[i]);
        appendStringInfoString(&namebuf, "_idx");

        /*
         * PostgreSQL identifiers are limited to NAMEDATALEN-1 (63) bytes.
         * If the name is too long, truncate and append a hash to avoid
         * collisions.
         */
        if (namebuf.len > NAMEDATALEN - 1)
        {
                uint32  hash = simple_hash(namebuf.data);

                /* _xxxx_idx = 10 chars, so truncate at 53 to leave room */
                namebuf.data[NAMEDATALEN - 1 - 10] = '\0';
                namebuf.len = NAMEDATALEN - 1 - 10;
                appendStringInfo(&namebuf, "_%04x_idx", hash);
        }

        idxname = quote_identifier(namebuf.data);

        /*
         * CREATE INDEX IF NOT EXISTS <name> ON <schema>.<table> (<col1>, <col2>)
         */
        initStringInfo(&buf);
        appendStringInfo(&buf, "CREATE INDEX IF NOT EXISTS %s ON %s%s%s (",
                          idxname,
                          (relation->schemaname ? quote_identifier(relation->schemaname) : ""),
                          (relation->schemaname ? "." : ""),
                          quote_identifier(relation->relname));
        for (i = 0; i < nCols; i++)
        {
                if (i > 0)
                        appendStringInfoString(&buf, ", ");
                appendStringInfoString(&buf, quote_identifier(colNames[i]));
        }
        appendStringInfoChar(&buf, ')');

        if (pg_fk_indexer_debug)
                elog(LOG, "pg_fk_indexer: executing: %s", buf.data);

        if ((ret = SPI_connect()) != SPI_OK_CONNECT)
                elog(ERROR, "pg_fk_indexer: SPI_connect failed with error %d", ret);

        ret = SPI_execute(buf.data, false, 0);

        if (ret != SPI_OK_UTILITY)
                elog(ERROR, "pg_fk_indexer: SPI_execute failed with error %d", ret);

        SPI_finish();
}


static bool
is_column_indexed(Oid relid, AttrNumber *attnums, int nKeys)
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
                int                     i;
                bool            match;

                /* Look up the specific index in the pg_index catalog */
                indexTuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(indexOid));
                if (!HeapTupleIsValid(indexTuple))
                        continue;

                indexForm = (Form_pg_index) GETSTRUCT(indexTuple);

                /*
                 * Check if the FK columns match the leading columns of this
                 * index.  An index on (a, b, c) covers FK (a, b), but an
                 * index on (a) alone does not cover FK (a, b).
                 */
                if (indexForm->indnatts < nKeys)
                {
                        ReleaseSysCache(indexTuple);
                        continue;
                }

                match = true;
                for (i = 0; i < nKeys; i++)
                {
                        if (indexForm->indkey.values[i] != attnums[i])
                        {
                                match = false;
                                break;
                        }
                }

                ReleaseSysCache(indexTuple);

                if (match)
                {
                        if (pg_fk_indexer_debug)
                                elog(LOG, "pg_fk_indexer: FK columns already covered by index %u",
                                         indexOid);
                        found = true;
                        break;
                }
        }

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
                        int                     nKeys;
                        int                     i;
                        char      **colNames;

                        if (pg_fk_indexer_debug)
                                elog(LOG, "pg_fk_indexer: found FK constraint \"%s\" on table %u",
                                         NameStr(con->conname), relid);

                        adatum = heap_getattr(tuple,
                                              Anum_pg_constraint_conkey,
                                              RelationGetDescr(pg_constraint_rel),
                                              &isNull);

                        if (!isNull)
                        {
                                arr = DatumGetArrayTypeP(adatum);
                                Assert(ARR_NDIM(arr) == 1 && !ARR_HASNULL(arr));

                                if (!(ARR_DIMS(arr)[0] >= 1))
                                        continue;

                                nKeys = ARR_DIMS(arr)[0];
                                attnums = (int16 *) ARR_DATA_PTR(arr);

                                if (!is_column_indexed(relid, (AttrNumber *) attnums, nKeys))
                                {
                                        colNames = palloc(sizeof(char *) * nKeys);
                                        for (i = 0; i < nKeys; i++)
                                                colNames[i] = get_attname(relid, attnums[i], false);

                                        inject_index(relation, colNames, nKeys);

                                        for (i = 0; i < nKeys; i++)
                                                pfree(colNames[i]);
                                        pfree(colNames);
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

                if (pg_fk_indexer_debug)
                        elog(LOG, "pg_fk_indexer: intercepted utility command: %s",
                                 nodeTag(parsetree) == T_CreateStmt ? "CREATE TABLE" :
                                 nodeTag(parsetree) == T_AlterTableStmt ? "ALTER TABLE" :
                                 "other");

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

/*  runs when extension is loaded into memory */
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

        DefineCustomBoolVariable("pg_fk_indexer.debug",
                                  "Enable debug logging for pg_fk_indexer",
                                  NULL,
                                  &pg_fk_indexer_debug,
                                  false,
                                  PGC_USERSET,
                                  0,
                                  NULL,
                                  NULL,
                                  NULL);

        prev_utility_hook = ProcessUtility_hook;

        ProcessUtility_hook = pg_fk_indexer_utility_hook;
}

/*  runs when extension is unloaded */
void
_PG_fini(void)
{
        ProcessUtility_hook = prev_utility_hook;
}
