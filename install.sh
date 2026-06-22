#!/usr/bin/env sh
set -eu

REPO="fennaraOfficial/fennara-godot-mcp"
VERSION="latest"
INSTALL_DIR="${FENNARA_INSTALL_DIR:-}"
MODIFY_PATH=1

usage() {
  cat <<'EOF'
Install Fennara CLI.

Usage:
  curl -fsSL https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.sh | sh
  curl -fsSL https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.sh | sh -s -- --version 0.2.8

Options:
  -v, --version <version>      Install a specific version without v prefix. Default: latest
  -d, --install-dir <path>     Install directory. Default: platform app-data directory
      --no-modify-path         Do not add ~/.local/bin to a shell profile
  -h, --help                   Show this help
EOF
}

require_value() {
  option="$1"
  value="${2:-}"
  if [ -z "$value" ]; then
    echo "error: $option requires a value" >&2
    exit 1
  fi
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    -v|--version)
      require_value "$1" "${2:-}"
      VERSION="$2"
      shift 2
      ;;
    --version=*)
      VERSION="${1#*=}"
      shift
      ;;
    -d|--install-dir)
      require_value "$1" "${2:-}"
      INSTALL_DIR="$2"
      shift 2
      ;;
    --install-dir=*)
      INSTALL_DIR="${1#*=}"
      shift
      ;;
    --no-modify-path)
      MODIFY_PATH=0
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

need() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "error: $1 is required" >&2
    exit 1
  fi
}

need curl
need unzip

os="$(uname -s)"
case "$os" in
  Darwin) platform="macos" ;;
  Linux) platform="linux" ;;
  *) echo "error: unsupported OS: $os" >&2; exit 1 ;;
esac

machine="$(uname -m)"
case "$machine" in
  x86_64|amd64) arch="x86_64" ;;
  arm64|aarch64) arch="arm64" ;;
  *) echo "error: unsupported architecture: $machine" >&2; exit 1 ;;
esac

if [ "$VERSION" = "latest" ]; then
  release_api="https://api.github.com/repos/$REPO/releases/tags/latest"
else
  release_api="https://api.github.com/repos/$REPO/releases/tags/v$VERSION"
fi

if [ -n "$INSTALL_DIR" ]; then
  app_dir="$INSTALL_DIR"
elif [ "$platform" = "macos" ]; then
  app_dir="$HOME/Library/Application Support/Fennara"
else
  app_dir="${XDG_DATA_HOME:-$HOME/.local/share}/fennara"
fi

bin_dir="$app_dir/bin"
link_dir="$HOME/.local/bin"
tmp_dir="$(mktemp -d)"

cleanup() {
  rm -rf "$tmp_dir"
}
trap cleanup EXIT INT TERM

echo "Fetching Fennara release metadata..."
release_json="$tmp_dir/release.json"
curl -fsSL -H "User-Agent: fennara-install" "$release_api" -o "$release_json"

asset_url="$(
  sed -nE 's/.*"browser_download_url"[[:space:]]*:[[:space:]]*"([^"]*fennara-cli-'"$platform"'-'"$arch"'-v[^"]*\.zip)".*/\1/p' "$release_json" |
    head -n 1
)"

if [ -z "$asset_url" ]; then
  echo "error: could not find fennara-cli-$platform-$arch asset" >&2
  exit 1
fi

zip_path="$tmp_dir/fennara-cli.zip"
extract_dir="$tmp_dir/extract"
mkdir -p "$extract_dir" "$bin_dir" "$link_dir"

echo "Downloading $(basename "$asset_url")..."
curl -fL -H "User-Agent: fennara-install" "$asset_url" -o "$zip_path"
unzip -q "$zip_path" -d "$extract_dir"

if [ ! -f "$extract_dir/bin/fennara" ]; then
  echo "error: downloaded package is missing fennara" >&2
  exit 1
fi

cp "$extract_dir/bin/fennara" "$bin_dir/fennara"
chmod +x "$bin_dir/fennara"
ln -sf "$bin_dir/fennara" "$link_dir/fennara"

echo "Verifying Fennara CLI..."
"$bin_dir/fennara" --version

ensure_profile_path() {
  profile_path=""
  shell_name="$(basename "${SHELL:-}")"
  case "$shell_name" in
    zsh) profile_path="$HOME/.zshrc" ;;
    bash) profile_path="$HOME/.bashrc" ;;
    fish) profile_path="$HOME/.config/fish/config.fish" ;;
    *) profile_path="$HOME/.profile" ;;
  esac

  mkdir -p "$(dirname "$profile_path")"
  touch "$profile_path"

  if grep -F "fennara path start" "$profile_path" >/dev/null 2>&1; then
    return
  fi

  if [ "$shell_name" = "fish" ]; then
    cat >> "$profile_path" <<EOF

# fennara path start
fish_add_path "$link_dir"
# fennara path end
EOF
  else
    cat >> "$profile_path" <<EOF

# fennara path start
export PATH="\$HOME/.local/bin:\$PATH"
# fennara path end
EOF
  fi

  echo "Added $link_dir to $profile_path. Open a new terminal before running fennara by name."
}

case ":$PATH:" in
  *":$link_dir:"*) ;;
  *)
    if [ "$MODIFY_PATH" -eq 1 ]; then
      ensure_profile_path
    else
      echo ""
      echo "Skipped PATH update. Add this to your shell profile when you want to run fennara by name:"
      echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    fi
    ;;
esac

if command -v fennara >/dev/null 2>&1; then
  echo "fennara command: available"
else
  echo "fennara command: not available in this shell yet. Open a new terminal."
fi

echo "Installed Fennara CLI to $bin_dir"
echo 'Run `fennara install` inside a Godot project or pass `--project <path>`.'
