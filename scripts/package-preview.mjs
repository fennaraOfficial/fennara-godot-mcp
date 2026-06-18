import { copyFileSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync } from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const args = parseArgs(process.argv.slice(2));
const version = readVersion();
const platform = requiredArg("platform");
const arch = args.arch ?? "x86_64";
const distDir = path.join(root, "dist");
const stageRoot = path.join(root, ".package-preview");

rmSync(stageRoot, { recursive: true, force: true });
mkdirSync(distDir, { recursive: true });

const addonArchive = packageAddon();
const localArchive = packageLocal();

console.log(`Created ${path.relative(root, addonArchive)}`);
console.log(`Created ${path.relative(root, localArchive)}`);

function packageAddon() {
  const stage = path.join(stageRoot, "addon");
  const source = path.join(root, "godot", "addons", "fennara");
  const target = path.join(stage, "addons", "fennara");

  mkdirSync(path.join(target, "bin"), { recursive: true });
  copyDir(source, target, (sourcePath) => {
    const relative = path.relative(source, sourcePath).replaceAll(path.sep, "/");
    if (relative === "bin" || relative.startsWith("bin/")) {
      return relative === "bin" || isAddonBinary(relative);
    }
    return true;
  });

  const archive = path.join(distDir, `fennara-addon-${platform}-${arch}-v${version}.zip`);
  zipDirectory(stage, archive);
  return archive;
}

function packageLocal() {
  const stage = path.join(stageRoot, "local");
  const binDir = path.join(stage, "bin");
  const releaseDir = path.join(root, "local", "target", "release");
  const extension = platform === "windows" ? ".exe" : "";
  const binaries = [
    "fennara-daemon",
    "fennara-daemon-runtime",
    "fennara-mcp",
    "fennara-mcp-runtime",
  ];

  mkdirSync(binDir, { recursive: true });
  for (const binary of binaries) {
    const source = path.join(releaseDir, `${binary}${extension}`);
    if (!exists(source)) {
      throw new Error(`Missing local binary: ${path.relative(root, source)}`);
    }
    copyFile(source, path.join(binDir, `${binary}${extension}`));
  }

  copyFile(path.join(root, "VERSION"), path.join(stage, "VERSION"));

  const archive = path.join(distDir, `fennara-local-${platform}-${arch}-v${version}.zip`);
  zipDirectory(stage, archive);
  return archive;
}

function isAddonBinary(relative) {
  if (platform === "windows") {
    return relative === "bin/libfennara.windows.editor.x86_64.dll";
  }
  if (platform === "linux") {
    return relative === "bin/libfennara.linux.editor.x86_64.so";
  }
  if (platform === "macos") {
    return relative.startsWith("bin/libfennara.macos.editor.framework/");
  }
  throw new Error(`Unsupported platform: ${platform}`);
}

function zipDirectory(sourceDir, archivePath) {
  rmSync(archivePath, { force: true });

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

function copyDir(source, target, filter) {
  for (const entry of readdirSync(source)) {
    const sourcePath = path.join(source, entry);
    if (!filter(sourcePath)) {
      continue;
    }

    const targetPath = path.join(target, entry);
    if (statSync(sourcePath).isDirectory()) {
      mkdirSync(targetPath, { recursive: true });
      copyDir(sourcePath, targetPath, filter);
    } else {
      copyFile(sourcePath, targetPath);
    }
  }
}

function copyFile(source, target) {
  mkdirSync(path.dirname(target), { recursive: true });
  copyFileSync(source, target);
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

function requiredArg(name) {
  if (!args[name]) {
    throw new Error(`Missing --${name}`);
  }
  return args[name];
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
