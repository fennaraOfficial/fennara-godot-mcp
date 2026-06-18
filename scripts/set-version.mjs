import { spawnSync } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const version = process.argv[2];

if (!version || !/^\d+\.\d+\.\d+$/.test(version)) {
  console.error("Usage: node scripts/set-version.mjs <x.y.z>");
  process.exit(1);
}

write("VERSION", `${version}\n`);

update("local/Cargo.toml", (text) => {
  if (/\[workspace\.package\][\s\S]*?version\s*=/.test(text)) {
    return text.replace(
      /(\[workspace\.package\][\s\S]*?version\s*=\s*)"[^"]+"/,
      `$1"${version}"`,
    );
  }

  return text.replace(
    /(\[workspace\.package\]\r?\n)/,
    `$1version = "${version}"\n`,
  );
});

for (const manifest of [
  "local/crates/fennara-cli/Cargo.toml",
  "local/crates/fennara-daemon/Cargo.toml",
  "local/crates/fennara-mcp/Cargo.toml",
]) {
  update(manifest, (text) => {
    if (/version\.workspace\s*=\s*true/.test(text)) {
      return text.replace(/version\s*=\s*"[^"]+"\r?\n/g, "");
    }

    return text.replace(/version\s*=\s*"[^"]+"/, "version.workspace = true");
  });
}

update("fennara-cpp/include/fennara/local_bridge.hpp", (text) =>
  text.replace(/PLUGIN_VERSION\s*=\s*"[^"]+"/, `PLUGIN_VERSION = "${version}"`),
);

run("cargo", ["update", "-w", "--manifest-path", path.join("local", "Cargo.toml")]);
run(process.execPath, [path.join("scripts", "check-version.mjs")]);

function read(relativePath) {
  return readFileSync(path.join(root, relativePath), "utf8");
}

function write(relativePath, text) {
  writeFileSync(path.join(root, relativePath), text);
}

function update(relativePath, updater) {
  write(relativePath, updater(read(relativePath)));
}

function run(command, args) {
  const result = spawnSync(command, args, {
    cwd: root,
    stdio: "inherit",
    shell: process.platform === "win32",
  });

  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}
