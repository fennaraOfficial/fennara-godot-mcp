import { copyFileSync, mkdirSync, readdirSync, rmSync, statSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const source = path.join(root, "ui", "chat");
const target = path.join(root, "godot", "addons", "fennara", "dist");

rmSync(target, { recursive: true, force: true });
copyDir(source, target);

console.log(`Synced ${path.relative(root, source)} -> ${path.relative(root, target)}`);

function copyDir(from, to) {
  mkdirSync(to, { recursive: true });
  for (const entry of readdirSync(from)) {
    if (entry === "README.md") {
      continue;
    }

    const sourcePath = path.join(from, entry);
    const targetPath = path.join(to, entry);
    if (statSync(sourcePath).isDirectory()) {
      copyDir(sourcePath, targetPath);
    } else {
      copyFileSync(sourcePath, targetPath);
    }
  }
}
