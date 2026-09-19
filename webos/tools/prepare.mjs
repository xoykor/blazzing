import { copyFileSync, existsSync, mkdirSync } from "node:fs";
import { dirname, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const root = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const vendor = resolve(root, "app", "vendor");
mkdirSync(vendor, { recursive: true });

const files = [
  ["node_modules/webostvjs/webOSTV.js", "webOSTV.js"],
  ["node_modules/webostvjs/webOSTV-dev.js", "webOSTV-dev.js"],
  ["node_modules/qrcode-generator/qrcode.js", "qrcode.js"]
];

for (const [source, destination] of files) {
  const from = resolve(root, source);
  if (!existsSync(from)) {
    throw new Error("Missing dependency file: " + source);
  }
  copyFileSync(from, resolve(vendor, destination));
}

console.log("Prepared webOS vendor files in app/vendor.");
