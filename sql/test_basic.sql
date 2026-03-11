-- Load the extension
CREATE EXTENSION pg_fk_indexer;
LOAD 'pg_fk_indexer';

-- Setup: parent table
CREATE TABLE users(id int PRIMARY KEY, username text);

-- Test 1: CREATE TABLE with named FK constraint
CREATE TABLE orders(order_id int, user_id int,
  CONSTRAINT fk_user FOREIGN KEY (user_id) REFERENCES users(id));

-- Verify index was created
SELECT indexname FROM pg_indexes
  WHERE tablename = 'orders' AND indexdef LIKE '%user_id%';

DROP TABLE orders;

-- Test 2: CREATE TABLE with inline column FK reference
CREATE TABLE orders(user_id int REFERENCES users(id));

SELECT indexname FROM pg_indexes
  WHERE tablename = 'orders' AND indexdef LIKE '%user_id%';

DROP TABLE orders;

-- Test 3a: ALTER TABLE ADD CONSTRAINT ... FOREIGN KEY
CREATE TABLE orders(user_id int);
ALTER TABLE orders ADD CONSTRAINT fk_user
  FOREIGN KEY (user_id) REFERENCES users(id);

SELECT indexname FROM pg_indexes
  WHERE tablename = 'orders' AND indexdef LIKE '%user_id%';

DROP TABLE orders;

-- Test 3b: ALTER TABLE ADD FOREIGN KEY (no constraint name)
CREATE TABLE orders(user_id int);
ALTER TABLE orders ADD FOREIGN KEY (user_id) REFERENCES users(id);

SELECT indexname FROM pg_indexes
  WHERE tablename = 'orders' AND indexdef LIKE '%user_id%';

DROP TABLE orders;

-- Test 4: Column that already has an index (should NOT create duplicate)
CREATE TABLE orders(user_id int);
CREATE INDEX orders_user_id_idx ON orders(user_id);
ALTER TABLE orders ADD FOREIGN KEY (user_id) REFERENCES users(id);

-- Should show exactly 1 index, not 2
SELECT count(*) FROM pg_indexes
  WHERE tablename = 'orders' AND indexdef LIKE '%user_id%';

DROP TABLE orders;

-- Test 5: Multiple FK columns on one CREATE TABLE
CREATE TABLE another_parent(id int PRIMARY KEY);
CREATE TABLE multi_fk(
  a_id int REFERENCES users(id),
  b_id int REFERENCES another_parent(id)
);

SELECT indexname FROM pg_indexes
  WHERE tablename = 'multi_fk'
  ORDER BY indexname;

DROP TABLE multi_fk;
DROP TABLE another_parent;

-- Test 6a: Composite PK with one FK column
CREATE TABLE products(id int PRIMARY KEY, name text);
CREATE TABLE order_items(
  order_id int,
  product_id int,
  quantity int,
  PRIMARY KEY (order_id, product_id),
  CONSTRAINT fk_product FOREIGN KEY (product_id) REFERENCES products(id)
);

SELECT indexname FROM pg_indexes
  WHERE tablename = 'order_items' AND indexname LIKE '%_idx';

DROP TABLE order_items;
DROP TABLE products;

-- Test 6b: Composite PK with both columns as FKs (junction table)
-- user_id is the leading column of the composite PK so it's already covered;
-- only product_id needs a separate index (mirrors MySQL behavior)
CREATE TABLE products(id int PRIMARY KEY, name text);
CREATE TABLE user_products(
  user_id int,
  product_id int,
  PRIMARY KEY (user_id, product_id),
  CONSTRAINT fk_user FOREIGN KEY (user_id) REFERENCES users(id),
  CONSTRAINT fk_product FOREIGN KEY (product_id) REFERENCES products(id)
);

SELECT indexname FROM pg_indexes
  WHERE tablename = 'user_products' AND indexname LIKE '%_idx'
  ORDER BY indexname;

DROP TABLE user_products;
DROP TABLE products;

-- Test 7a: Composite FK on CREATE TABLE
CREATE TABLE parent_ab(a int, b int, PRIMARY KEY (a, b));
CREATE TABLE child_ab(
  a int,
  b int,
  data text,
  CONSTRAINT fk_ab FOREIGN KEY (a, b) REFERENCES parent_ab(a, b)
);

SELECT indexname, indexdef FROM pg_indexes
  WHERE tablename = 'child_ab' AND indexname LIKE '%_idx'
  ORDER BY indexname;

DROP TABLE child_ab;
DROP TABLE parent_ab;

-- Test 7b: Composite FK where matching index already exists (no duplicate)
CREATE TABLE parent_ab(a int, b int, PRIMARY KEY (a, b));
CREATE TABLE child_ab(a int, b int, data text);
CREATE INDEX child_ab_a_b_idx ON child_ab(a, b);
ALTER TABLE child_ab ADD CONSTRAINT fk_ab FOREIGN KEY (a, b) REFERENCES parent_ab(a, b);

SELECT count(*) FROM pg_indexes
  WHERE tablename = 'child_ab' AND indexdef LIKE '%a, b%';

DROP TABLE child_ab;
DROP TABLE parent_ab;

-- Test 7c: ALTER TABLE with composite FK
CREATE TABLE parent_ab(a int, b int, PRIMARY KEY (a, b));
CREATE TABLE child_ab(a int, b int, data text);
ALTER TABLE child_ab ADD FOREIGN KEY (a, b) REFERENCES parent_ab(a, b);

SELECT indexname FROM pg_indexes
  WHERE tablename = 'child_ab' AND indexname LIKE '%_idx'
  ORDER BY indexname;

DROP TABLE child_ab;
DROP TABLE parent_ab;

-- Test 7d: Three-column composite FK
CREATE TABLE parent_abc(a int, b int, c int, PRIMARY KEY (a, b, c));
CREATE TABLE child_abc(
  a int,
  b int,
  c int,
  data text,
  CONSTRAINT fk_abc FOREIGN KEY (a, b, c) REFERENCES parent_abc(a, b, c)
);

SELECT indexname, indexdef FROM pg_indexes
  WHERE tablename = 'child_abc' AND indexname LIKE '%_idx'
  ORDER BY indexname;

DROP TABLE child_abc;
DROP TABLE parent_abc;

-- Test 7e: Single-column index exists but composite FK needs (a, b) — should still create
CREATE TABLE parent_ab(a int, b int, PRIMARY KEY (a, b));
CREATE TABLE child_ab(a int, b int, data text);
CREATE INDEX child_ab_a_idx ON child_ab(a);
ALTER TABLE child_ab ADD FOREIGN KEY (a, b) REFERENCES parent_ab(a, b);

SELECT indexname FROM pg_indexes
  WHERE tablename = 'child_ab' AND indexname LIKE '%_idx'
  ORDER BY indexname;

DROP TABLE child_ab;
DROP TABLE parent_ab;

-- Test 8: Long names that exceed NAMEDATALEN (63 bytes) — should truncate with hash
CREATE TABLE long_parent(id int PRIMARY KEY);
CREATE TABLE this_is_a_very_long_table_name_that_will_exceed(
  and_this_is_a_very_long_column_name_too int
    REFERENCES long_parent(id)
);

SELECT indexname FROM pg_indexes
  WHERE tablename = 'this_is_a_very_long_table_name_that_will_exceed' AND indexname LIKE '%_idx';

DROP TABLE this_is_a_very_long_table_name_that_will_exceed;
DROP TABLE long_parent;

-- Cleanup
DROP TABLE users;
DROP EXTENSION pg_fk_indexer;
