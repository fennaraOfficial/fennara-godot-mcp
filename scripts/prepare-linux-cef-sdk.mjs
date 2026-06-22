import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync } from "node:fs";
import path from "node:path";
import { spawnSync } from "node:child_process";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const args = parseArgs(process.argv.slice(2));
const version = args.version ?? "139.0.28+g55ab8a8+chromium-139.0.7258.139";
const expectedSha256 =
  args.sha256 ?? "14ed6824d7f12bb7f143934e9a7952b562b9081fc32cc7b623b81d255049329d";
const outDir = path.resolve(root, args["out-dir"] ?? ".package-preview/cef-sdk");
const downloadDir = path.resolve(root, args["download-dir"] ?? ".package-preview/cef-download");
const archiveName = `cef_binary_${version}_linux64_minimal.tar.bz2`;
const archivePath = path.join(downloadDir, archiveName);
const downloadUrl = `https://cef-builds.spotifycdn.com/${archiveName.replaceAll("+", "%2B")}`;

mkdirSync(downloadDir, { recursive: true });
mkdirSync(outDir, { recursive: true });

if (!existsSync(archivePath)) {
  run("curl", ["--location", "--fail", "--show-error", "--retry", "3", "--output", archivePath, downloadUrl]);
}
verifySha256(archivePath, expectedSha256);

let cefRoot = findCefRoot(outDir);
if (!cefRoot) {
  rmSync(outDir, { recursive: true, force: true });
  mkdirSync(outDir, { recursive: true });
  run("tar", ["-xjf", archivePath, "-C", outDir]);
  cefRoot = findCefRoot(outDir);
}

if (!cefRoot || !existsSync(path.join(cefRoot, "libcef_dll"))) {
  throw new Error(`Extracted CEF SDK is missing libcef_dll/: ${outDir}`);
}

console.log(cefRoot);
if (process.env.GITHUB_OUTPUT) {
  await import("node:fs").then(({ appendFileSync }) => {
    appendFileSync(process.env.GITHUB_OUTPUT, `cef_root=${cefRoot}\n`);
  });
}

function findCefRoot(source) {
  if (!existsSync(source) || !statSync(source).isDirectory()) {
    return null;
  }
  for (const entry of readdirSync(source)) {
    const candidate = path.join(source, entry);
    if (statSync(candidate).isDirectory() && entry.startsWith("cef_binary_")) {
      return candidate;
    }
  }
  return null;
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

function verifySha256(filePath, expected) {
  const actual = createHash("sha256").update(readFileSync(filePath)).digest("hex");
  if (actual.toLowerCase() !== expected.toLowerCase()) {
    throw new Error(`CEF SDK sha256 mismatch for ${path.basename(filePath)}: expected ${expected}, got ${actual}`);
  }
}

function parseArgs(rawArgs) {
  const parsed = {};
  for (let i = 0; i < rawArgs.length; i += 1) {
    const arg = rawArgs[i];
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
