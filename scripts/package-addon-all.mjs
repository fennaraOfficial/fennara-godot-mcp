import { copyFileSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync } from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const args = parseArgs(process.argv.slice(2));
const version = readVersion();
const partsDir = path.resolve(root, args["parts-dir"] ?? "dist");
const distDir = path.join(root, "dist");
const stageDir = path.join(root, ".package-preview", "addon-all");
const archive = path.join(distDir, `fennara-addon-v${version}.zip`);
const latestArchive = path.join(distDir, "fennara-addon-latest.zip");

rmSync(stageDir, { recursive: true, force: true });
mkdirSync(stageDir, { recursive: true });

const partRoots = findAddonParts(partsDir);
if (partRoots.length === 0) {
  throw new Error(`No addon parts found in ${path.relative(root, partsDir)}`);
}

for (const partRoot of partRoots) {
  copyDir(partRoot, stageDir);
}
assertNoBundledCef(path.join(stageDir, "addons", "fennara"));
assertBundledRipgrep(path.join(stageDir, "addons", "fennara"));

zipDirectory(stageDir, archive);
copyFileSync(archive, latestArchive);
console.log(`Created ${path.relative(root, archive)}`);
console.log(`Created ${path.relative(root, latestArchive)}`);

function findAddonParts(source) {
  const results = [];
  visit(source);
  return results;

  function visit(current) {
    if (!exists(current) || !statSync(current).isDirectory()) {
      return;
    }

    const addonRoot = path.join(current, "addons", "fennara");
    if (exists(path.join(addonRoot, "fennara.gdextension"))) {
      results.push(current);
      return;
    }

    for (const entry of readdirSync(current)) {
      visit(path.join(current, entry));
    }
  }
}

function zipDirectory(sourceDir, archivePath) {
  rmSync(archivePath, { force: true });
  mkdirSync(path.dirname(archivePath), { recursive: true });

  const script = [
    "import os, sys, zipfile",
    "source, archive = sys.argv[1], sys.argv[2]",
    "with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zf:",
    "    for root, _, files in os.walk(source):",
    "        for name in files:",
    "            path = os.path.join(root, name)",
    "            arcname = os.path.relpath(path, source).replace(os.sep, '/')",
    "            zf.write(path, arcname)",
  ].join("\n");

  run("python", ["-c", script, sourceDir, archivePath]);
}

function copyDir(source, target) {
  for (const entry of readdirSync(source)) {
    const sourcePath = path.join(source, entry);
    const targetPath = path.join(target, entry);
    if (statSync(sourcePath).isDirectory()) {
      mkdirSync(targetPath, { recursive: true });
      copyDir(sourcePath, targetPath);
    } else {
      mkdirSync(path.dirname(targetPath), { recursive: true });
      copyFileSync(sourcePath, targetPath);
    }
  }
}

function run(command, commandArgs) {
  const result = spawnSync(command, commandArgs, {
    cwd: root,
    stdio: "inherit",
  });
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

function parseArgs(rawArgs) {
  const parsed = {};
  for (let i = 0; i < rawArgs.length; i += 1) {
    const arg = rawArgs[i];
    if (!arg.startsWith("--")) {
      throw new Error(`Unexpected argument: ${arg}`);
    }
    if (!rawArgs[i + 1] || rawArgs[i + 1].startsWith("--")) {
      throw new Error(`Missing value for ${arg}`);
    }
    parsed[arg.slice(2)] = rawArgs[i + 1];
    i += 1;
  }
  return parsed;
}

function readVersion() {
  return readFileSync(path.join(root, "VERSION"), "utf8").trim();
}

function exists(filePath) {
  try {
    statSync(filePath);
    return true;
  } catch {
    return false;
  }
}

function assertNoBundledCef(addonRoot) {
  const forbidden = new Set([
    "libcef.so",
    "fennara_cef_helper",
    "chrome-sandbox",
    "icudtl.dat",
    "resources.pak",
    "v8_context_snapshot.bin",
  ]);
  visit(addonRoot);

  function visit(current) {
    for (const entry of readdirSync(current)) {
      const filePath = path.join(current, entry);
      if (statSync(filePath).isDirectory()) {
        visit(filePath);
      } else if (forbidden.has(entry)) {
        throw new Error(
          `CEF runtime file ${path.relative(root, filePath)} must not be packaged inside fennara-addon-*`
        );
      }
    }
  }
}

function assertBundledRipgrep(addonRoot) {
  const required = [
    "bin/rg-windows-x86_64.exe",
    "bin/rg-linux-x86_64",
    "bin/rg-macos-arm64",
  ];
  for (const relative of required) {
    const filePath = path.join(addonRoot, ...relative.split("/"));
    if (!exists(filePath)) {
      throw new Error(`Missing bundled ripgrep binary in addon archive: ${relative}`);
    }
  }
}
