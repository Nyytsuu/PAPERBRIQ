const { loadEnvFile } = require("./src/config/env");

loadEnvFile();

const { dataStore } = require("./src/db");
const { createServer } = require("./src/server/createServer");

const PORT = Number(process.env.PORT || 4173);
const HOST = process.env.HOST || "0.0.0.0";
const server = createServer();

server.listen(PORT, HOST, () => {
  console.log(`PaperBriq app running at http://localhost:${PORT}`);
  console.log(`Listening on host: ${HOST}`);
  console.log(`Data mode: ${dataStore.mode}`);
});
