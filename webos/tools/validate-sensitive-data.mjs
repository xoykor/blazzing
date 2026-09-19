import fs from "node:fs";
import path from "node:path";

const root = process.cwd();
const roots = [
  path.join(root, "app", "src"),
  path.join(root, "service", "io.github.xoykor.blazzing.network")
];

function collectRuntimeFiles(directory) {
  return fs.readdirSync(directory)
    .filter((name) => name.endsWith(".js"))
    .sort()
    .map((name) => path.join(directory, name));
}

const files = roots.flatMap(collectRuntimeFiles);
const failures = [];
const privateKeyMarkers = [
  "-----BEGIN RSA PRIVATE KEY-----",
  "-----BEGIN PRIVATE KEY-----",
  "-----BEGIN EC PRIVATE KEY-----",
  "-----BEGIN OPENSSH PRIVATE KEY-----"
];
const namedSecretLiteral =
  /\b(password|passwd|pwd|credential|token|secret|apiKey|apikey)\b\s*[:=]\s*(["'])([^\r\n"']{6,})\2/gi;

for (const file of files) {
  const source = fs.readFileSync(file, "utf8");
  const relative = path.relative(root, file);

  for (const marker of privateKeyMarkers) {
    if (source.includes(marker)) {
      failures.push(relative + ": embedded private-key material");
    }
  }

  let match;
  while ((match = namedSecretLiteral.exec(source)) !== null) {
    failures.push(
      relative + ": non-empty literal assigned to " + match[1]
    );
  }
  namedSecretLiteral.lastIndex = 0;

  const base = path.basename(file).toLowerCase();
  if (/(password|passwd|credential|private[-_]?key|secret|token)/.test(base)) {
    failures.push(relative + ": sensitive keyword in runtime filename");
  }
}

if (failures.length) {
  throw new Error(
    "Sensitive-data gate failed:\n" +
    failures.map((line) => " - " + line).join("\n")
  );
}

console.log(
  "Sensitive-data gate passed for " +
  files.length +
  " runtime JavaScript files."
);
