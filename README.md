## pg_fk_indexer

[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-14--18-blue.svg)](https://www.postgresql.org/)
[![License](https://img.shields.io/badge/license-PostgreSQL-green.svg)](LICENSE)

PostgreSQL extension that automatically creates indexes on foreign key columns — bringing a behavior that MySQL has provided by default since day one.


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

Load the library for the current session:

```sql
LOAD 'pg_fk_indexer';

-- Foreign keys are now auto-indexed
CREATE TABLE users (id int PRIMARY KEY, username text);
CREATE TABLE orders (user_id int REFERENCES users(id));
-- ^^ index on orders(user_id) is created automatically
```

Or (recommended) load it automatically for all sessions by adding to `postgresql.conf`:

```
shared_preload_libraries = 'pg_fk_indexer'
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









### OLD

neovim integration for clangd  
bear --make

make
sudo make install
sudo -u postgres psql -d postgres

LOAD 'pg_fk_indexer';

### Testcases

create table users(id int PRIMARY KEY, username text);

fk1:: 
create table orders(order_id int, user_id int, CONSTRAINT fk_user FOREIGN KEY (user_id) REFERENCES users(id));

fk2:: 
CREATE TABLE orders (user_id int REFERENCES users(id));

fk3.1::
CREATE TABLE orders (user_id int);
ALTER TABLE orders
ADD CONSTRAINT fk_user FOREIGN KEY (user_id) REFERENCES users(id);

fk3.2::
CREATE TABLE orders (user_id int);
ALTER TABLE orders
ADD FOREIGN KEY (user_id) REFERENCES users(id);

fk4.1:: (composite key - one column is foreign key)
CREATE TABLE users (id int PRIMARY KEY, username text);
CREATE TABLE products (id int PRIMARY KEY, name text);
CREATE TABLE order_items (
    order_id int,
    product_id int,
    quantity int,
    PRIMARY KEY (order_id, product_id),
    CONSTRAINT fk_product FOREIGN KEY (product_id) REFERENCES products(id)
);

fk4.2:: (composite key - both columns are foreign keys)
CREATE TABLE users (id int PRIMARY KEY, username text);
CREATE TABLE products (id int PRIMARY KEY, name text);
CREATE TABLE user_products (
    user_id int,
    product_id int,
    PRIMARY KEY (user_id, product_id),
    CONSTRAINT fk_user FOREIGN KEY (user_id) REFERENCES users(id),
    CONSTRAINT fk_product FOREIGN KEY (product_id) REFERENCES products(id)
);

### Run tests

make installcheck

