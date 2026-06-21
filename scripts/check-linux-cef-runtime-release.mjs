import { createHash } from "node:crypto";
import { createWriteStream, existsSync, mkdirSync, readFileSync, rmSync, statSync } from "node:fs";
import http from "node:http";
import https from "node:https";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
if (process.argv.includes("--help") || process.argv.includes("-h")) {
  printHelp();
  process.exit(0);
}
const args = parseArgs(process.argv.slice(2));
const assetsDir = path.resolve(root, args["assets-dir"] ?? "release-assets");
const downloadUrl = args["download-url"] ?? "";
const manifest = JSON.parse(readFileSync(path.join(root, "local", "webview-runtimes", "linux-cef.json"), "utf8"));

if (!manifest.enabled) {
  if (downloadUrl) {
    throw new Error("linux_cef_runtime_url was provided, but local/webview-runtimes/linux-cef.json is disabled.");
  }
  console.log("Linux CEF runtime manifest is disabled; release asset check skipped.");
  process.exit(0);
}

const archive = manifest.archive ?? {};
const assetName = requiredString(archive.name, "archive.name");
const expectedSha256 = requiredString(archive.sha256, "archive.sha256");
const version = requiredString(manifest.version, "version");
if (version.startsWith("TODO")) {
  throw new Error("Linux CEF runtime manifest is enabled but version is still TODO.");
}

mkdirSync(assetsDir, { recursive: true });
const assetPath = path.join(assetsDir, assetName);
if (downloadUrl) {
  rmSync(assetPath, { force: true });
  await download(downloadUrl, assetPath);
}

if (!existsSync(assetPath) || !statSync(assetPath).isFile()) {
  throw new Error(
    `Linux CEF runtime manifest is enabled, but release assets do not include ${assetName}. ` +
      "Pass the Release workflow linux_cef_runtime_url input or disable the manifest."
  );
}

const actualSha256 = sha256File(assetPath);
if (actualSha256.toLowerCase() !== expectedSha256.toLowerCase()) {
  throw new Error(`Linux CEF runtime sha256 mismatch for ${assetName}: expected ${expectedSha256}, got ${actualSha256}`);
}

console.log(`Linux CEF runtime release asset verified: ${assetName}`);

function requiredString(value, field) {
  if (typeof value !== "string" || value.length === 0) {
    throw new Error(`Linux CEF runtime manifest is enabled but ${field} is not set.`);
  }
  return value;
}

function sha256File(file) {
  return createHash("sha256").update(readFileSync(file)).digest("hex");
}

function download(url, target) {
  return new Promise((resolve, reject) => {
    const client = url.startsWith("https:") ? https : http;
    client
      .get(url, { headers: { "User-Agent": "fennara-release" } }, (response) => {
        if ([301, 302, 303, 307, 308].includes(response.statusCode ?? 0) && response.headers.location) {
          response.resume();
          download(new URL(response.headers.location, url).toString(), target).then(resolve, reject);
          return;
        }
        if (response.statusCode !== 200) {
          response.resume();
          reject(new Error(`failed to download ${url}: HTTP ${response.statusCode}`));
          return;
        }
        const file = createWriteStream(target);
        response.pipe(file);
        file.on("finish", () => file.close(resolve));
        file.on("error", reject);
      })
      .on("error", reject);
  });
}

function parseArgs(rawArgs) {
  const parsed = {};
  for (let i = 0; i < rawArgs.length; i += 1) {
    const arg = rawArgs[i];
    if (!arg.startsWith("--")) {
      throw new Error(`Unexpected argument: ${arg}`);
    }
    const value = rawArgs[i + 1];
    if (value === undefined || value.startsWith("--")) {
      throw new Error(`Missing value for ${arg}`);
    }
    parsed[arg.slice(2)] = value;
    i += 1;
  }
  return parsed;
}

function printHelp() {
  console.log(`\
Validate the optional Linux CEF runtime release asset.

Usage:
  node scripts/check-linux-cef-runtime-release.mjs --assets-dir release-assets --download-url ""
  node scripts/check-linux-cef-runtime-release.mjs --assets-dir release-assets --download-url <prebuilt-zip-url>

If local/webview-runtimes/linux-cef.json is enabled, the asset named in the
manifest must be present and match archive.sha256.
`);
}
