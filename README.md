## pg_fk_indexer - auto-index FK columns

[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-14--18-blue.svg)](https://www.postgresql.org/)
[![License](https://img.shields.io/badge/license-PostgreSQL-green.svg)](LICENSE)

PostgreSQL extension that automatically creates indexes on foreign key columns - bringing a behavior that MySQL has provided by default since day one.

<img width="322" height="309" alt="pg_fk_indexer" src="https://github.com/user-attachments/assets/fadcbc64-1f13-49ed-9e86-e4f8816be446" />


### Why?

PostgreSQL does not automatically index foreign key columns. Unindexed FKs can cause slow joins, slow deletes on the parent table, and sequential scans where you'd expect index lookups. This extension makes indexing FKs automatic.

### Features

- Intercepts `CREATE TABLE` and `ALTER TABLE` statements transparently
- Handles all FK declaration styles: inline references, named constraints, anonymous constraints
- Handles composite primary key / junction tables correctly — skips FK columns already covered as the leading column of an existing index
- Skips index creation if the column is already indexed (no duplicates)
- Can be toggled on/off per session via GUC
- Zero configuration required

### Installation

```bash
make
sudo make install
```

### Usage

Load extension automatically for all sessions by adding to `postgresql.conf`:

```
shared_preload_libraries = 'pg_fk_indexer'
```


```sql
-- Foreign keys are now auto-indexed
CREATE TABLE users (id int PRIMARY KEY, username text);
CREATE TABLE orders (user_id int REFERENCES users(id));
--  index on orders(user_id) is created automatically
```

Or load the library for the current session only:

```sql
LOAD 'pg_fk_indexer';
```

### Configuration

```sql
-- Disable for the current session
SET pg_fk_indexer.enabled = off;

-- Re-enable
SET pg_fk_indexer.enabled = on;

-- Check current state
SHOW pg_fk_indexer.enabled;
```

### Running tests

```bash
make installcheck
```

