const fs = require("fs");
const path = require("path");
const mysql = require("mysql2/promise");
const { loadEnvFile } = require("../backend/src/config/env");
const { parseMysqlDatabaseUrl } = require("../backend/src/config/database");

async function main() {
  loadEnvFile();

  const databaseUrl = process.env.DATABASE_URL;
  if (!databaseUrl || !databaseUrl.startsWith("mysql://")) {
    throw new Error("DATABASE_URL must be a MySQL connection string.");
  }

  const schemaPath = path.join(__dirname, "..", "database", "mysql", "mysql_schema.sql");
  const schemaSql = fs.readFileSync(schemaPath, "utf8");
  const config = parseMysqlDatabaseUrl(databaseUrl);
  const database = config.database;
  const connection = await mysql.createConnection({
    ...config,
    database: undefined,
    multipleStatements: true
  });

  try {
    await connection.query(schemaSql);
    const [tables] = await connection.query(
      `SELECT TABLE_NAME
       FROM INFORMATION_SCHEMA.TABLES
       WHERE TABLE_SCHEMA = ?
       ORDER BY TABLE_NAME`,
      [database]
    );

    console.log(`Migrated MySQL schema: ${database}`);
    console.log(`Tables found: ${tables.length}`);
    console.log(tables.map((row) => `- ${row.TABLE_NAME}`).join("\n"));
  } finally {
    await connection.end();
  }
}

main().catch((error) => {
  console.error(error.message || error);
  process.exit(1);
});
