EXTENSION = pg_fk_indexer
MODULES = pg_fk_indexer
DATA = pg_fk_indexer--1.0.sql

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
