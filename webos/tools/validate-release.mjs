import fs from "node:fs";
import path from "node:path";

const root = process.cwd();
const readJson = (relativePath) =>
  JSON.parse(fs.readFileSync(path.join(root, relativePath), "utf8"));

const app = readJson("app/appinfo.json");
const devPackage = readJson("package.json");
const servicePackage = readJson(
  "service/io.github.xoykor.blazzing.network/package.json"
);
const services = readJson(
  "service/io.github.xoykor.blazzing.network/services.json"
);

const expectedAppId = "io.github.xoykor.blazzing";
const expectedServiceId = "io.github.xoykor.blazzing.network";
const requiredAppFields = [
  "id",
  "version",
  "vendor",
  "type",
  "main",
  "title",
  "icon",
  "requiredACG"
];

for (const field of requiredAppFields) {
  if (!(field in app)) {
    throw new Error("Missing appinfo field: " + field);
  }
}

if (app.id !== expectedAppId) {
  throw new Error("Unexpected app id: " + app.id);
}

if (app.type !== "web") {
  throw new Error("webOS app type must be 'web'.");
}

if (!Array.isArray(app.requiredACG) || app.requiredACG.length !== 0) {
  throw new Error("The webOS app must not request privileged ACG groups.");
}

if (servicePackage.name !== expectedServiceId) {
  throw new Error("Unexpected service package id: " + servicePackage.name);
}

if (services.id !== expectedServiceId) {
  throw new Error("services.json id must match the service package id.");
}

if (devPackage.version !== app.version ||
    servicePackage.version !== app.version) {
  throw new Error(
    "Release versions must match: app=" + app.version +
    ", dev=" + devPackage.version +
    ", service=" + servicePackage.version
  );
}

const semver = /^\d+\.\d+\.\d+$/;
if (!semver.test(app.version)) {
  throw new Error("App version must be numeric x.y.z: " + app.version);
}

const requiredFiles = [
  "app/index.html",
  "app/icon.png",
  "app/css/app.css",
  "app/vendor/webOSTV.js",
  "app/vendor/webOSTV-dev.js",
  "app/vendor/qrcode.js",
  "service/io.github.xoykor.blazzing.network/network_service.js",
  "service/io.github.xoykor.blazzing.network/services.json",
  "prepare.fish",
  "package.fish",
  "run-simulator.fish"
];

for (const relativePath of requiredFiles) {
  if (!fs.existsSync(path.join(root, relativePath))) {
    throw new Error("Missing release input: " + relativePath);
  }
}

console.log(
  "webOS release metadata valid — " +
  app.id + " " + app.version +
  " + " + servicePackage.name
);
