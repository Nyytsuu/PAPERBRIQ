const { getDatabaseClient } = require("../config/database");
const { createFallbackStore } = require("./fallbackStore");
const { createMysqlStore } = require("./mysqlStore");
const { createPostgresStore } = require("./postgresStore");

function createDataStore() {
  const fallbackStore = createFallbackStore();
  const databaseUrl = process.env.DATABASE_URL;
  const requestedClient = getDatabaseClient();
  let store = fallbackStore;

  if (requestedClient === "mysql") {
    store = createMysqlStore(databaseUrl) || fallbackStore;
  }

  if (requestedClient === "postgres") {
    store = createPostgresStore(databaseUrl) || fallbackStore;
  }

  return withFallbackMethods(store, fallbackStore);
}

function withFallbackMethods(store, fallbackStore) {
  for (const key of Object.keys(fallbackStore)) {
    if (typeof fallbackStore[key] === "function" && typeof store[key] !== "function") {
      store[key] = fallbackStore[key].bind(fallbackStore);
    }
  }
  return store;
}

const dataStore = createDataStore();

module.exports = {
  dataStore
};
