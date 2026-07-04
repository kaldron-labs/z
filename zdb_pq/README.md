For Postgres testing:
1. Postgres uses path to control which installation is used
2. Ensure that the correct `pg_config` is found first in the path
  - e.g. `~/postgres/bin` if debugging locally
3. Ensure `pg_config --bindir` is correct
4. Ensure postgres is running: `pg_ctl -D [datadir] start`
5. Check databases: `psql 'host=/tmp dbname=postgres'`: `\l`
  - `CREATE DATABASE test;` to create a new test database
6. Ensure `pguint`, `libz` extensions and their dependencies are installed:
  - `uint.so`, `libz.so`, `libZu.so` should all exist in `pg_config --libdir`
  - if not, install them using `make && make install && make installcheck`
7. Ensure extensions are enabled in the test database:
  - `psql 'host=/tmp dbname=test'`:
    ```
    CREATE EXTENSION uint;
    CREATE EXTENSION libz;
    ```
    - Check enabled extensions with `\dx`
8. Run `zdbpqtest` with `-m zdb_pq/src/.libs/libZdbPQ.so` `-c 'host=/tmp dbname=test`
