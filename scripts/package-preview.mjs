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

syncChatUi();

const addonPart = packageAddonPart();
const cliArchive = packageCli();
const localArchive = packageLocal();

console.log(`Created ${path.relative(root, addonPart)}`);
console.log(`Created ${path.relative(root, cliArchive)}`);
console.log(`Created ${path.relative(root, localArchive)}`);

function packageAddonPart() {
  const source = path.join(root, "godot", "addons", "fennara");
  const target = path.join(distDir, `addon-part-${platform}-${arch}`, "addons", "fennara");

  rmSync(path.dirname(path.dirname(target)), { recursive: true, force: true });
  mkdirSync(path.join(target, "bin"), { recursive: true });
  copyDir(source, target, (sourcePath) => {
    const relative = path.relative(source, sourcePath).replaceAll(path.sep, "/");
    if (relative === "bin" || relative.startsWith("bin/")) {
      return relative === "bin" || isAddonBinary(relative);
    }
    return true;
  });
  bundleRipgrep(target);
  assertNoBundledCef(target);

  return path.dirname(path.dirname(target));
}

function syncChatUi() {
  const source = path.join(root, "ui", "chat");
  const target = path.join(root, "godot", "addons", "fennara", "dist");

  rmSync(target, { recursive: true, force: true });
  copyDir(source, target, (sourcePath) => {
    const relative = path.relative(source, sourcePath).replaceAll(path.sep, "/");
    return relative !== "README.md";
  });

  console.log(`Synced ${path.relative(root, source)} -> ${path.relative(root, target)}`);
}

function packageCli() {
  const stage = path.join(stageRoot, "cli");
  const binDir = path.join(stage, "bin");
  const releaseDir = path.join(root, "local", "target", "release");
  const extension = platform === "windows" ? ".exe" : "";
  const binary = `fennara${extension}`;

  mkdirSync(binDir, { recursive: true });
  copyFile(path.join(releaseDir, binary), path.join(binDir, binary));
  copyFile(path.join(root, "VERSION"), path.join(stage, "VERSION"));

  const archive = path.join(distDir, `fennara-cli-${platform}-${arch}-v${version}.zip`);
  zipDirectory(stage, archive);
  return archive;
}

function packageLocal() {
  const stage = path.join(stageRoot, "local");
  const binDir = path.join(stage, "bin");
  const releaseDir = path.join(root, "local", "target", "release");
  const extension = platform === "windows" ? ".exe" : "";
  const binaries = [
    "fennara",
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
    return (
      relative === "bin/libfennara.linux.editor.x86_64.so" ||
      relative === "bin/libfennara_linux_cef_bridge.so"
    );
  }
  if (platform === "macos") {
    return (
      relative === "bin/libfennara.macos.editor.framework" ||
      relative.startsWith("bin/libfennara.macos.editor.framework/")
    );
  }
  throw new Error(`Unsupported platform: ${platform}`);
}

function bundleRipgrep(addonRoot) {
  const source = findRipgrep();
  const target = path.join(addonRoot, "bin", ripgrepBinaryName());
  copyFile(source, target);
  if (platform !== "windows") {
    chmodExecutable(target);
  }
  console.log(`Bundled ${path.relative(root, target)}`);
}

function ripgrepBinaryName() {
  if (platform === "windows") {
    return `rg-${platform}-${arch}.exe`;
  }
  return `rg-${platform}-${arch}`;
}

function findRipgrep() {
  if (process.env.FENNARA_RIPGREP_PATH) {
    const configured = path.resolve(process.env.FENNARA_RIPGREP_PATH);
    if (!exists(configured)) {
      throw new Error(`FENNARA_RIPGREP_PATH does not exist: ${configured}`);
    }
    return configured;
  }

  if (platform === "windows") {
    return commandPath("where.exe", ["rg.exe"], "rg.exe");
  }
  return commandPath("sh", ["-c", "command -v rg"], "rg");
}

function commandPath(command, commandArgs, label) {
  const result = spawnSync(command, commandArgs, {
    cwd: root,
    encoding: "utf8",
  });
  if (result.status !== 0) {
    throw new Error(`Could not find ${label}. Install ripgrep before packaging.`);
  }

  const found = result.stdout.split(/\r?\n/).map((line) => line.trim()).find(Boolean);
  if (!found || !exists(found)) {
    throw new Error(`Could not resolve ${label} path from command output.`);
  }
  return found;
}

function chmodExecutable(filePath) {
  const result = spawnSync("chmod", ["755", filePath], {
    cwd: root,
    stdio: "inherit",
  });
  if (result.status !== 0) {
    throw new Error(`Failed to mark ${path.relative(root, filePath)} executable.`);
  }
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
