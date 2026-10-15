const fs = require("fs");
const path = require("path");

const PUBLIC_DIR = path.join(__dirname, "..", "..", "..", "frontend", "web");
const BUILD_DIR = path.join(PUBLIC_DIR, "dist");

function serveStatic(req, res, requestUrl) {
  const pathname = requestUrl.pathname === "/" ? "/index.html" : requestUrl.pathname;
  const decodedPath = decodeURIComponent(pathname);
  const candidates = [
    path.normalize(path.join(BUILD_DIR, decodedPath)),
    path.normalize(path.join(PUBLIC_DIR, decodedPath))
  ];

  if (candidates.some((filePath) => !isAllowedPath(filePath))) {
    res.writeHead(403);
    res.end("Forbidden");
    return;
  }

  readFirst(candidates, (error, filePath, content) => {
    if (error) {
      readFirst([path.join(BUILD_DIR, "index.html"), path.join(PUBLIC_DIR, "index.html")], (fallbackError, fallbackPath, fallbackContent) => {
        if (fallbackError) {
          res.writeHead(404);
          res.end("Not found");
          return;
        }
        res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
        res.end(fallbackContent);
      });
      return;
    }

    res.writeHead(200, { "Content-Type": getContentType(filePath) });
    res.end(content);
  });
}

function readFirst(filePaths, callback) {
  const remaining = [...filePaths];

  function next() {
    const filePath = remaining.shift();
    if (!filePath) {
      callback(new Error("No readable static file"));
      return;
    }

    fs.readFile(filePath, (error, content) => {
      if (error) {
        next();
        return;
      }
      callback(null, filePath, content);
    });
  }

  next();
}

function isAllowedPath(filePath) {
  return filePath.startsWith(PUBLIC_DIR);
}

function getContentType(filePath) {
  const ext = path.extname(filePath).toLowerCase();
  const types = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "application/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".map": "application/json; charset=utf-8",
    ".png": "image/png",
    ".jpg": "image/jpeg",
    ".jpeg": "image/jpeg",
    ".webp": "image/webp",
    ".svg": "image/svg+xml"
  };
  return types[ext] || "application/octet-stream";
}

module.exports = {
  serveStatic
};
