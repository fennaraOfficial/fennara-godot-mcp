import { createHash } from "node:crypto";
import {
  existsSync,
  mkdirSync,
  readFileSync,
  statSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const DEFAULT_MINIMUM_CLI_VERSION = "0.3.2";
const RELEASE_PLATFORMS = [
  { key: "windows-x86_64", platform: "windows", arch: "x86_64" },
  { key: "linux-x86_64", platform: "linux", arch: "x86_64" },
  { key: "macos-arm64", platform: "macos", arch: "arm64" },
];

if (process.argv.includes("--help") || process.argv.includes("-h")) {
  printHelp();
  process.exit(0);
}

const args = parseArgs(process.argv.slice(2));
const version = args.version ?? readVersion();
const assetsDir = path.resolve(root, args["assets-dir"] ?? "release-assets");
const outPath = path.resolve(
  root,
  args.out ?? path.join("dist", `fennara-release-manifest-v${version}.json`),
);
const linuxCefManifestPath = path.resolve(
  root,
  args["linux-cef-manifest"] ?? path.join("local", "webview-runtimes", "linux-cef.json"),
);
const minimumCliVersion = args["minimum-cli-version"] ?? DEFAULT_MINIMUM_CLI_VERSION;

validateVersion(version, "version");
validateVersion(minimumCliVersion, "minimum CLI version");
if (!existsSync(assetsDir) || !statSync(assetsDir).isDirectory()) {
  throw new Error(`--assets-dir must be an existing directory: ${assetsDir}`);
}

const manifest = buildManifest();
validateManifestAssets(manifest, assetsDir);
mkdirSync(path.dirname(outPath), { recursive: true });
writeFileSync(outPath, `${JSON.stringify(manifest, null, 2)}\n`);

console.log(`Created ${path.relative(root, outPath)}`);

function buildManifest() {
  const assets = {
    cli: {},
    local: {},
    addon: assetRecord(`fennara-release-addon-v${version}.zip`),
  };

  for (const target of RELEASE_PLATFORMS) {
    assets.cli[target.key] = assetRecord(
      `fennara-cli-${target.platform}-${target.arch}-v${version}.zip`,
      target,
    );
    assets.local[target.key] = assetRecord(
      `fennara-release-local-${target.platform}-${target.arch}-v${version}.zip`,
      target,
    );
  }

  const sharedRuntimes = sharedRuntimeRecords();
  const installPrimitives = ["local-zip-v1", "addon-zip-v1"];
  if (sharedRuntimes.some((runtime) => runtime.kind === "cef")) {
    installPrimitives.push("shared-webview-cef-v1");
  }

  return {
    schema_version: 1,
    version,
    minimum_cli_version: minimumCliVersion,
    install_primitives: installPrimitives,
    assets,
    shared_runtimes: sharedRuntimes,
  };
}

function sharedRuntimeRecords() {
  if (!existsSync(linuxCefManifestPath)) {
    return [];
  }

  const linuxCef = JSON.parse(readFileSync(linuxCefManifestPath, "utf8"));
  if (!linuxCef.enabled) {
    return [];
  }

  const archive = linuxCef.archive ?? {};
  const assetName = requiredString(archive.name, "Linux CEF archive.name");
  const asset = assetRecord(assetName);
  const expectedSha256 = requiredString(archive.sha256, "Linux CEF archive.sha256");
  if (asset.sha256.toLowerCase() !== expectedSha256.toLowerCase()) {
    throw new Error(
      `Linux CEF manifest sha256 mismatch for ${assetName}: expected ${expectedSha256}, got ${asset.sha256}`,
    );
  }

  return [
    {
      id: "linux-cef",
      kind: "cef",
      schema_version: linuxCef.schema_version ?? 1,
      platform: linuxCef.platform ?? "linux",
      arch: linuxCef.arch ?? "x86_64",
      platform_arch: linuxCef.platform_arch ?? "linux-x64",
      version: requiredString(linuxCef.version, "Linux CEF version"),
      enabled: true,
      layout:
        linuxCef.layout ??
        "webview/cef/linux-x64/<cef-version> with webview/cef/linux-x64/current.json pointing at the selected version",
      required_files: requiredArray(linuxCef.required_files, "Linux CEF required_files"),
      archive: {
        format: archive.format ?? "zip",
        name: asset.name,
        sha256: asset.sha256,
      },
    },
  ];
}

function assetRecord(name, target = {}) {
  const file = path.join(assetsDir, name);
  if (!existsSync(file) || !statSync(file).isFile()) {
    throw new Error(`Release manifest asset is missing: ${path.relative(root, file)}`);
  }
  const { key, ...targetFields } = target;
  return {
    ...targetFields,
    ...(key ? { platform_arch: key } : {}),
    name,
    sha256: sha256File(file),
  };
}

function validateManifestAssets(manifest, baseDir) {
  const records = [
    ...Object.values(manifest.assets.cli ?? {}),
    ...Object.values(manifest.assets.local ?? {}),
    manifest.assets.addon,
    ...manifest.shared_runtimes.map((runtime) => runtime.archive),
  ].filter(Boolean);

  for (const record of records) {
    const name = requiredString(record.name, "manifest asset name");
    const expectedSha256 = requiredString(record.sha256, `manifest sha256 for ${name}`);
    const file = path.join(baseDir, name);
    if (!existsSync(file) || !statSync(file).isFile()) {
      throw new Error(`Manifest references missing asset: ${name}`);
    }
    const actualSha256 = sha256File(file);
    if (actualSha256.toLowerCase() !== expectedSha256.toLowerCase()) {
      throw new Error(
        `Manifest sha256 mismatch for ${name}: expected ${expectedSha256}, got ${actualSha256}`,
      );
    }
  }
}

function sha256File(file) {
  return createHash("sha256").update(readFileSync(file)).digest("hex");
}

function readVersion() {
  return readFileSync(path.join(root, "VERSION"), "utf8").trim();
}

function requiredString(value, label) {
  if (typeof value !== "string" || value.length === 0) {
    throw new Error(`${label} is required`);
  }
  return value;
}

function requiredArray(value, label) {
  if (!Array.isArray(value) || value.length === 0) {
    throw new Error(`${label} must be a non-empty array`);
  }
  return value;
}

function validateVersion(value, label) {
  if (!/^\d+\.\d+\.\d+$/.test(value)) {
    throw new Error(`Invalid ${label}: ${value}`);
  }
}

function parseArgs(rawArgs) {
  const parsed = {};
  for (let index = 0; index < rawArgs.length; index += 1) {
    const arg = rawArgs[index];
    if (!arg.startsWith("--")) {
      throw new Error(`Unexpected argument: ${arg}`);
    }
    const value = rawArgs[index + 1];
    if (value === undefined || value.startsWith("--")) {
      throw new Error(`Missing value for ${arg}`);
    }
    parsed[arg.slice(2)] = value;
    index += 1;
  }
  return parsed;
}

function printHelp() {
  console.log(`\
Write the release manifest consumed by fennara install/update.

Usage:
  node scripts/write-release-manifest.mjs --assets-dir release-assets --out release-assets/fennara-release-manifest-v0.3.2.json
  node scripts/write-release-manifest.mjs --assets-dir release-assets --linux-cef-manifest generated-linux-cef-manifest/linux-cef.json

Options:
  --version <x.y.z>                Release version. Default: VERSION.
  --assets-dir <dir>               Directory containing release zip assets. Default: release-assets.
                                   Release local/addon assets must use fennara-release-local-* and fennara-release-addon-* names.
  --linux-cef-manifest <path>      Generated enabled Linux CEF manifest. Default: local/webview-runtimes/linux-cef.json.
  --minimum-cli-version <x.y.z>    Minimum CLI for schema/primitives. Default: ${DEFAULT_MINIMUM_CLI_VERSION}.
  --out <path>                     Output manifest path. Default: dist/fennara-release-manifest-v<version>.json.
`);
}
