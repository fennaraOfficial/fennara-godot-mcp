import { createHash } from "node:crypto";
import {
  chmodSync,
  copyFileSync,
  existsSync,
  mkdirSync,
  readdirSync,
  readFileSync,
  rmSync,
  statSync,
} from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
if (process.argv.includes("--help") || process.argv.includes("-h")) {
  printHelp();
  process.exit(0);
}
const args = parseArgs(process.argv.slice(2));
const cefRoot = requiredPath("cef-root");
const version = requiredArg("version");
const outDir = path.resolve(root, args["out-dir"] ?? "dist/cef-runtime");
const dryRun = Boolean(args["dry-run"]);
const platformArch = "linux-x64";
const assetName = `fennara-webview-cef-linux-x64-${version}.zip`;
const stageDir = path.join(root, ".package-preview", "linux-cef-runtime");
const archivePath = path.join(outDir, assetName);
const manifest = readManifest();
const requiredFiles = manifest.required_files ?? [];

validateVersion(version);
const cefReleaseDir = firstExisting(path.join(cefRoot, "Release"), cefRoot);
const cefResourcesDir = firstExisting(path.join(cefRoot, "Resources"), cefRoot);
const helperPath = args.helper ? path.resolve(root, args.helper) : path.join(stageDir, "fennara_cef_helper");

const plan = buildPlan();
if (dryRun) {
  printPlan(plan);
  process.exit(0);
}

rmSync(stageDir, { recursive: true, force: true });
mkdirSync(stageDir, { recursive: true });
mkdirSync(outDir, { recursive: true });

if (args.helper) {
  copyFile(args.helper, helperPath);
  chmodSync(helperPath, 0o755);
} else {
  buildHelper(helperPath);
}

for (const item of plan) {
  copyFile(item.source, path.join(stageDir, item.relative));
}
validateRequiredFiles(stageDir, requiredFiles);
zipDirectory(stageDir, archivePath);
const sha256 = sha256File(archivePath);

console.log(`Created ${path.relative(root, archivePath)}`);
console.log(`sha256: ${sha256}`);
console.log("");
console.log("Update local/webview-runtimes/linux-cef.json with:");
console.log(`  enabled: true`);
console.log(`  version: ${version}`);
console.log(`  archive.name: ${assetName}`);
console.log(`  archive.sha256: ${sha256}`);

function buildPlan() {
  const files = new Map();
  for (const relative of requiredFiles) {
    if (relative === "fennara_cef_helper") {
      continue;
    }
    files.set(relative, locateCefFile(relative));
  }

  for (const relative of [
    "chrome-sandbox",
    "libEGL.so",
    "libGLESv2.so",
    "libvk_swiftshader.so",
    "libvulkan.so.1",
    "snapshot_blob.bin",
    "vk_swiftshader_icd.json",
  ]) {
    const source = locateCefFile(relative, false);
    if (source) {
      files.set(relative, source);
    }
  }

  const localesDir = path.join(cefResourcesDir, "locales");
  if (existsSync(localesDir)) {
    for (const entry of readdirSync(localesDir)) {
      if (entry.endsWith(".pak")) {
        files.set(`locales/${entry}`, path.join(localesDir, entry));
      }
    }
  }

  return [...files.entries()]
    .map(([relative, source]) => ({ relative, source }))
    .sort((a, b) => a.relative.localeCompare(b.relative));
}

function locateCefFile(relative, required = true) {
  const candidates = [
    path.join(cefReleaseDir, relative),
    path.join(cefResourcesDir, relative),
    path.join(cefRoot, relative),
  ];
  for (const candidate of candidates) {
    if (existsSync(candidate) && statSync(candidate).isFile()) {
      return candidate;
    }
  }
  if (required) {
    throw new Error(`Missing CEF file ${relative} under ${cefRoot}`);
  }
  return null;
}

function buildHelper(target) {
  if (process.platform !== "linux") {
    throw new Error("Building fennara_cef_helper requires Linux; pass --helper <path> for a prebuilt helper.");
  }
  const source = path.join(root, "scripts", "cef", "linux", "fennara_cef_helper.cpp");
  run("c++", [
    "-std=c++17",
    "-O2",
    `-I${path.join(root, "fennara-cpp", "vendor", "cef")}`,
    source,
    "-Wl,-rpath,$ORIGIN",
    "-ldl",
    "-o",
    target,
  ]);
  chmodSync(target, 0o755);
}

function validateRequiredFiles(baseDir, files) {
  for (const relative of files) {
    const file = path.join(baseDir, relative);
    if (!existsSync(file) || !statSync(file).isFile()) {
      throw new Error(`Assembled runtime is missing ${relative}`);
    }
  }
}

function zipDirectory(sourceDir, archive) {
  rmSync(archive, { force: true });
  const script = [
    "import os, stat, sys, zipfile",
    "source, archive = sys.argv[1], sys.argv[2]",
    "with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as zf:",
    "    for root, _, files in os.walk(source):",
    "        for name in files:",
    "            path = os.path.join(root, name)",
    "            arcname = os.path.relpath(path, source).replace(os.sep, '/')",
    "            info = zipfile.ZipInfo(arcname)",
    "            mode = os.stat(path).st_mode",
    "            info.external_attr = (mode & 0o777) << 16",
    "            with open(path, 'rb') as fh:",
    "                zf.writestr(info, fh.read(), zipfile.ZIP_DEFLATED)",
  ].join("\n");
  run("python", ["-c", script, sourceDir, archive]);
}

function printPlan(plan) {
  console.log(`CEF root: ${cefRoot}`);
  console.log(`version: ${version}`);
  console.log(`asset: ${path.relative(root, archivePath)}`);
  console.log(`helper: ${args.helper ? helperPath : "build from scripts/cef/linux/fennara_cef_helper.cpp"}`);
  console.log("files:");
  for (const item of plan) {
    console.log(`  ${item.relative} <- ${item.source}`);
  }
}

function readManifest() {
  return JSON.parse(readFileSync(path.join(root, "local", "webview-runtimes", "linux-cef.json"), "utf8"));
}

function sha256File(file) {
  return createHash("sha256").update(readFileSync(file)).digest("hex");
}

function copyFile(source, target) {
  mkdirSync(path.dirname(target), { recursive: true });
  copyFileSync(path.resolve(root, source), target);
}

function firstExisting(...candidates) {
  for (const candidate of candidates) {
    if (existsSync(candidate) && statSync(candidate).isDirectory()) {
      return candidate;
    }
  }
  return candidates.at(-1);
}

function validateVersion(value) {
  if (!value || value.includes("/") || value.includes("\\") || value === "." || value === "..") {
    throw new Error(`Unsafe CEF version for runtime directory/archive name: ${value}`);
  }
}

function requiredPath(name) {
  const value = path.resolve(root, requiredArg(name));
  if (!existsSync(value) || !statSync(value).isDirectory()) {
    throw new Error(`--${name} must be an existing directory`);
  }
  return value;
}

function requiredArg(name) {
  if (!args[name]) {
    throw new Error(`Missing --${name}`);
  }
  return args[name];
}

function parseArgs(rawArgs) {
  const parsed = {};
  for (let i = 0; i < rawArgs.length; i += 1) {
    const arg = rawArgs[i];
    if (arg === "--dry-run") {
      parsed["dry-run"] = true;
      continue;
    }
    if (!arg.startsWith("--")) {
      throw new Error(`Unexpected argument: ${arg}`);
    }
    const value = rawArgs[i + 1];
    if (!value || value.startsWith("--")) {
      throw new Error(`Missing value for ${arg}`);
    }
    parsed[arg.slice(2)] = value;
    i += 1;
  }
  return parsed;
}

function run(command, commandArgs) {
  const result = spawnSync(command, commandArgs, { cwd: root, stdio: "inherit" });
  if (result.status !== 0) {
    process.exit(result.status ?? 1);
  }
}

function printHelp() {
  console.log(`\
Prepare the separate Linux x64 CEF runtime asset.

Usage:
  node scripts/prepare-linux-cef-runtime.mjs --cef-root <cef_binary_dir> --version <cef-version> [--out-dir <dir>]
  node scripts/prepare-linux-cef-runtime.mjs --cef-root <cef_binary_dir> --version <cef-version> --helper <fennara_cef_helper> [--dry-run]

The CEF directory may be an official cef_binary_*_linux64 or
cef_binary_*_linux64_minimal tree with Release/ and Resources/ subdirectories,
or a flat directory containing the required runtime files.
`);
}
