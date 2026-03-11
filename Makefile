EXTENSION = pg_fk_indexer
MODULES = pg_fk_indexer
DATA = pg_fk_indexer--1.0.sql
REGRESS = test_basic

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# Paths for pgindent
PG_SRC = $(HOME)/postgresql
PGINDENT = $(PG_SRC)/src/tools/pgindent/pgindent
TYPEDEFS = $(PG_SRC)/src/tools/pgindent/typedefs.list

.PHONY: format
format:
	@echo "Running pgindent..."
	$(PGINDENT) --typedefs $(TYPEDEFS) $(MODULES).c
	@rm -f $(MODULES).c.bak
	@echo "Done."
