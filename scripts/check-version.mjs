import { readFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const version = read("VERSION").trim();
const failures = [];

if (!/^\d+\.\d+\.\d+$/.test(version)) {
  failures.push(`VERSION must use x.y.z format, got "${version}"`);
}

expect(
  "local/Cargo.toml",
  new RegExp(`\\[workspace\\.package\\][\\s\\S]*?version\\s*=\\s*"${escapeRegExp(version)}"`),
  `workspace.package version must be ${version}`,
);

for (const manifest of [
  "local/crates/fennara-daemon/Cargo.toml",
  "local/crates/fennara-mcp/Cargo.toml",
]) {
  expect(manifest, /version\.workspace\s*=\s*true/, "crate version must use workspace.package");
}

for (const name of ["fennara-daemon", "fennara-mcp"]) {
  expect(
    "local/Cargo.lock",
    new RegExp(`name = "${escapeRegExp(name)}"\\r?\\nversion = "${escapeRegExp(version)}"`),
    `${name} lockfile version must be ${version}`,
  );
}

expect(
  "fennara-cpp/include/fennara/local_bridge.hpp",
  new RegExp(`PLUGIN_VERSION\\s*=\\s*"${escapeRegExp(version)}"`),
  `plugin version must be ${version}`,
);

if (failures.length > 0) {
  console.error("Version check failed:");
  for (const failure of failures) {
    console.error(`- ${failure}`);
  }
  process.exit(1);
}

console.log(`Version check passed for ${version}`);

function read(relativePath) {
  return readFileSync(path.join(root, relativePath), "utf8");
}

function expect(relativePath, pattern, message) {
  if (!pattern.test(read(relativePath))) {
    failures.push(`${relativePath}: ${message}`);
  }
}

function escapeRegExp(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}
