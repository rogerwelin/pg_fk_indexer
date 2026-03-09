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

-- Cleanup
DROP TABLE users;
DROP EXTENSION pg_fk_indexer;
