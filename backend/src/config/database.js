function getDatabaseClient() {
  const explicitClient = String(process.env.DB_CLIENT || "").trim().toLowerCase();
  if (explicitClient === "mysql" || explicitClient === "postgres") {
    return explicitClient;
  }

  const databaseUrl = String(process.env.DATABASE_URL || "").trim().toLowerCase();
  if (databaseUrl.startsWith("mysql://")) return "mysql";
  if (databaseUrl.startsWith("postgres://") || databaseUrl.startsWith("postgresql://")) return "postgres";
  return "none";
}

function parseMysqlDatabaseUrl(connectionString) {
  const parsed = new URL(connectionString);
  return {
    host: parsed.hostname,
    port: Number(parsed.port || 3306),
    user: decodeURIComponent(parsed.username),
    password: decodeURIComponent(parsed.password),
    database: parsed.pathname.replace(/^\//, ""),
    waitForConnections: true,
    connectionLimit: 10,
    charset: "utf8mb4"
  };
}

module.exports = {
  getDatabaseClient,
  parseMysqlDatabaseUrl
};
