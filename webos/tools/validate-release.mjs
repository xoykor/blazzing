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
  "largeIcon",
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
  "app/largeicon.png",
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

function pngSize(relativePath) {
  const file = fs.readFileSync(path.join(root, relativePath));
  const signature = "89504e470d0a1a0a";

  if (file.length < 24 || file.subarray(0, 8).toString("hex") !== signature) {
    throw new Error("Expected PNG file: " + relativePath);
  }

  return {
    width: file.readUInt32BE(16),
    height: file.readUInt32BE(20)
  };
}

function requirePngSize(relativePath, width, height) {
  const actual = pngSize(relativePath);

  if (actual.width !== width || actual.height !== height) {
    throw new Error(
      relativePath + " must be " + width + "x" + height +
      " PNG, got " + actual.width + "x" + actual.height
    );
  }
}

if (app.icon !== "icon.png") {
  throw new Error("appinfo icon must reference icon.png");
}

if (app.largeIcon !== "largeicon.png") {
  throw new Error("appinfo largeIcon must reference largeicon.png");
}

requirePngSize("app/icon.png", 80, 80);
requirePngSize("app/largeicon.png", 130, 130);

const sellerIconCandidate = "store/blazzing-icon-400-candidate.png";
if (fs.existsSync(path.join(root, sellerIconCandidate))) {
  requirePngSize(sellerIconCandidate, 400, 400);
}

console.log(
  "webOS release metadata valid — " +
  app.id + " " + app.version +
  " + " + servicePackage.name
);
