## TODO


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

fk4:: (composite)




