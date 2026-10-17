function readJson(req) {
  return new Promise((resolve, reject) => {
    let data = "";
    req.on("data", (chunk) => {
      data += chunk;
      if (data.length > 1_000_000) {
        req.destroy();
        const error = new Error("Request body too large");
        error.statusCode = 413;
        error.publicMessage = "Request body is too large.";
        reject(error);
      }
    });
    req.on("end", () => {
      if (!data) {
        resolve({});
        return;
      }
      try {
        resolve(JSON.parse(data));
      } catch (error) {
        const parseError = new Error("Invalid JSON body");
        parseError.statusCode = 400;
        parseError.publicMessage = "Request body must be valid JSON.";
        reject(parseError);
      }
    });
  });
}

function sendJson(res, status, payload) {
  res.writeHead(status, {
    "Content-Type": "application/json; charset=utf-8",
    "Cache-Control": "no-store"
  });
  res.end(JSON.stringify(payload));
}

module.exports = {
  readJson,
  sendJson
};
