import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const vendor = resolve(root, "app", "vendor");
mkdirSync(vendor, { recursive: true });

function copyFirst(candidates, destination) {
  for (const candidate of candidates) {
    const from = resolve(root, candidate);
    if (existsSync(from)) {
      copyFileSync(from, resolve(vendor, destination));
      return;
    }
  }
  throw new Error("Missing dependency file for " + destination);
}

copyFirst([
  "node_modules/webostvjs/webOSTV.js",
  "node_modules/webostvjs/dist/webOSTV.js",
  "node_modules/webostvjs/src/webOSTV.js"
], "webOSTV.js");

copyFirst([
  "node_modules/webostvjs/webOSTV-dev.js",
  "node_modules/webostvjs/dist/webOSTV-dev.js",
  "node_modules/webostvjs/src/webOSTV-dev.js"
], "webOSTV-dev.js");

copyFirst([
  "node_modules/qrcode-generator/dist/qrcode.js",
  "node_modules/qrcode-generator/qrcode.js"
], "qrcode.js");

console.log("Prepared webOS vendor files in app/vendor.");
