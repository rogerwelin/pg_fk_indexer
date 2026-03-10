## pg_fk_indexer: PostgreSQL extension for automatically indexing foreign keys

[![PostgreSQL](https://img.shields.io/badge/PostgreSQL-14--18-blue.svg)](https://www.postgresql.org/)
[![License](https://img.shields.io/badge/license-PostgreSQL-green.svg)](LICENSE)


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

