import fs from "node:fs";
import path from "node:path";

const root = process.cwd();
const roots = [
  path.join(root, "app", "src"),
  path.join(root, "service", "io.github.xoykor.blazzing.network")
];

const forbidden = [
  {
    name: "optional chaining (?.)",
    pattern: /\?\./
  },
  {
    name: "nullish coalescing (??)",
    pattern: /\?\?/
  },
  {
    name: "async functions",
    pattern: /\basync\s+function\b/
  },
  {
    name: "async arrow functions",
    pattern: /\basync\s*(?:\([^)]*\)|[A-Za-z_$][\w$]*)\s*=>/
  },
  {
    name: "await",
    pattern: /\bawait\b/
  },
  {
    name: "dynamic import()",
    pattern: /\bimport\s*\(/
  },
  {
    name: "BigInt literals",
    pattern: /\b\d+n\b/
  }
];

function collectJs(directory) {
  return fs.readdirSync(directory)
    .filter((name) => name.endsWith(".js"))
    .sort()
    .map((name) => path.join(directory, name));
}

const files = roots.flatMap(collectJs);
const failures = [];

for (const file of files) {
  const source = fs.readFileSync(file, "utf8");

  for (const rule of forbidden) {
    if (rule.pattern.test(source)) {
      failures.push(
        path.relative(root, file) + ": unsupported " + rule.name
      );
    }
  }
}

if (failures.length) {
  throw new Error(
    "Legacy webOS compatibility gate failed:\n" +
    failures.map((line) => " - " + line).join("\n")
  );
}

console.log(
  "Legacy webOS compatibility gate passed for " +
  files.length +
  " JavaScript files."
);
