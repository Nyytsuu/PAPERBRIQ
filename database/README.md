# PaperBriq Database

Database files are separated by engine so MySQL Workbench and PostgreSQL do not accidentally run each other's SQL.

## MySQL Workbench

Import:

```text
database/mysql/mysql_schema.sql
```

That file creates the `paperbriq` database, tables, view, and demo data. Re-run it after backend updates when new page tables are added.

Do not import PostgreSQL files with MySQL Workbench or `mysql.exe`. MySQL will fail on PostgreSQL syntax such as `CREATE EXTENSION`, `UUID`, `TIMESTAMPTZ`, `JSONB`, enums, and `crypt()`.

Steps:

1. Open MySQL Workbench.
2. Connect to your local MySQL server.
3. Open `database/mysql/mysql_schema.sql`.
4. Run the full script.
5. Start the backend with `npm start`.
6. In another terminal, verify the API with `npm run smoke:api`.

## PostgreSQL Option 1: Docker

From the project root:

```powershell
docker compose up -d postgres
```

The database will initialize from:

- `database/postgres/schema.sql`
- `database/postgres/seed.sql`

## PostgreSQL Option 2: pgAdmin or psql

Create a PostgreSQL database named `paperbriq`, then run `schema.sql` first and `seed.sql` second.

With `psql`:

```powershell
psql -U postgres -d paperbriq -f database/postgres/schema.sql
psql -U postgres -d paperbriq -f database/postgres/seed.sql
```
