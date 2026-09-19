import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import crypto from "node:crypto";
import { execFileSync } from "node:child_process";

const root = process.cwd();
const dist = path.join(root, "dist");

function findIpk() {
  const explicit = process.argv[2];
  if (explicit) {
    return path.resolve(root, explicit);
  }

  const matches = fs.existsSync(dist) ?
    fs.readdirSync(dist)
      .filter((name) => name.endsWith(".ipk"))
      .sort() :
    [];

  if (matches.length !== 1) {
    throw new Error(
      "Expected exactly one IPK in dist/, found " + matches.length
    );
  }

  return path.join(dist, matches[0]);
}

const ipk = findIpk();
if (!fs.existsSync(ipk)) {
  throw new Error("IPK not found: " + ipk);
}

const temp = fs.mkdtempSync(path.join(os.tmpdir(), "blazzing-ipk-"));

try {
  execFileSync("ar", ["x", ipk], {
    cwd: temp,
    stdio: "inherit"
  });

  const dataArchive = path.join(temp, "data.tar.gz");
  if (!fs.existsSync(dataArchive)) {
    throw new Error("IPK does not contain data.tar.gz");
  }

  const listing = execFileSync(
    "tar",
    ["-tzf", "data.tar.gz"],
    { cwd: temp, encoding: "utf8" }
  ).split(/\r?\n/);

  const packaged = new Set(listing.filter(Boolean));
  const required = [
    "usr/palm/applications/io.github.xoykor.blazzing/",
    "usr/palm/applications/io.github.xoykor.blazzing/appinfo.json",
    "usr/palm/applications/io.github.xoykor.blazzing/icon.png",
    "usr/palm/applications/io.github.xoykor.blazzing/largeicon.png",
    "usr/palm/applications/io.github.xoykor.blazzing/index.html",
    "usr/palm/services/io.github.xoykor.blazzing.network/",
    "usr/palm/services/io.github.xoykor.blazzing.network/network_service.js",
    "usr/palm/services/io.github.xoykor.blazzing.network/services.json"
  ];

  for (const entry of required) {
    if (!packaged.has(entry)) {
      throw new Error("Required IPK payload missing: " + entry);
    }
  }

  const data = fs.readFileSync(ipk);
  const digest = crypto.createHash("sha256").update(data).digest("hex");
  const manifest = digest + "  " + path.basename(ipk) + "\n";
  fs.writeFileSync(path.join(dist, "SHA256SUMS.txt"), manifest);

  console.log(
    "IPK payload valid — " +
    path.basename(ipk) +
    " (" + required.length + " required entries)"
  );
  console.log(manifest.trim());
} finally {
  fs.rmSync(temp, { recursive: true, force: true });
}
