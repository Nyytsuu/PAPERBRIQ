const { loadEnvFile } = require("./src/config/env");

loadEnvFile();

const { dataStore } = require("./src/db");
const { createServer } = require("./src/server/createServer");

const PORT = Number(process.env.PORT || 4173);
const server = createServer();

server.listen(PORT, () => {
  console.log(`PaperBriq app running at http://localhost:${PORT}`);
  console.log(`Data mode: ${dataStore.mode}`);
});
