const http = require("http");
const { URL } = require("url");
const { routeApi } = require("../http/apiRouter");
const { sendJson } = require("../http/json");
const { serveStatic } = require("../http/staticFiles");

function createServer() {
  return http.createServer(async (req, res) => {
    try {
      const requestUrl = new URL(req.url, `http://${req.headers.host}`);

      if (requestUrl.pathname.startsWith("/api/")) {
        await routeApi(req, res, requestUrl);
        return;
      }

      serveStatic(req, res, requestUrl);
    } catch (error) {
      const status = Number(error?.statusCode || 500);
      if (status >= 500) console.error("Unexpected server error", error);
      sendJson(res, status, { error: error?.publicMessage || "Unexpected server error" });
    }
  });
}

module.exports = {
  createServer
};
