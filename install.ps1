param(
  [string] $Version = "latest",
  [string] $InstallDir = "",
  [switch] $NoModifyPath,
  [switch] $Help
)

$ErrorActionPreference = "Stop"

if ($Help) {
  Write-Output @"
Install Fennara CLI.

Usage:
  irm https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-mcp/main/install.ps1 | iex
  ./install.ps1 -Version 0.2.8

Options:
  -Version <version>     Install a specific version without v prefix. Default: latest
  -InstallDir <path>     Install directory. Default: %LOCALAPPDATA%\Fennara
  -NoModifyPath          Do not add the Fennara bin directory to user PATH
  -Help                  Show this help
"@
  exit 0
}

$repo = "fennaraOfficial/fennara-godot-mcp"
$platform = "windows"
$arch = "x86_64"

if (-not $InstallDir -and -not $env:LOCALAPPDATA) {
  throw "LOCALAPPDATA is not set."
}

$releaseApi = if ($Version -eq "latest") {
  "https://api.github.com/repos/$repo/releases/tags/latest"
} else {
  "https://api.github.com/repos/$repo/releases/tags/v$Version"
}

Write-Host "Fetching Fennara release metadata..."
$release = Invoke-RestMethod -Uri $releaseApi -Headers @{ "User-Agent" = "fennara-install" }
$asset = $release.assets |
  Where-Object { $_.name -like "fennara-cli-$platform-$arch-v*.zip" } |
  Select-Object -First 1

if (-not $asset) {
  throw "Could not find fennara-cli-$platform-$arch asset in release $($release.tag_name)."
}

$appDir = if ($InstallDir) { $InstallDir } else { Join-Path $env:LOCALAPPDATA "Fennara" }
$binDir = Join-Path $appDir "bin"
$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "fennara-install-$([System.Guid]::NewGuid())"
$zipPath = Join-Path $tempDir $asset.name
$extractDir = Join-Path $tempDir "extract"

New-Item -ItemType Directory -Force -Path $tempDir, $extractDir, $binDir | Out-Null

try {
  Write-Host "Downloading $($asset.name)..."
  Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath -Headers @{ "User-Agent" = "fennara-install" }
  Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir -Force

  $source = Join-Path $extractDir "bin\fennara.exe"
  if (-not (Test-Path $source)) {
    throw "Downloaded package is missing fennara.exe."
  }

  $target = Join-Path $binDir "fennara.exe"
  Copy-Item -LiteralPath $source -Destination $target -Force

  $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $pathParts = if ($userPath) { $userPath -split ";" } else { @() }
  $hasBin = $pathParts | Where-Object {
    $_ -and ([System.IO.Path]::GetFullPath($_).TrimEnd('\') -ieq [System.IO.Path]::GetFullPath($binDir).TrimEnd('\'))
  }

  if (-not $NoModifyPath -and -not $hasBin) {
    $nextPath = if ($userPath) { "$userPath;$binDir" } else { $binDir }
    [Environment]::SetEnvironmentVariable("Path", $nextPath, "User")
    Write-Host "Added $binDir to your user PATH."
  } elseif ($NoModifyPath) {
    Write-Host "Skipped PATH update. Add this directory to PATH when you want to run fennara by name:"
    Write-Host "  $binDir"
  }

  if (-not (($env:Path -split ";") | Where-Object {
    $_ -and ([System.IO.Path]::GetFullPath($_).TrimEnd('\') -ieq [System.IO.Path]::GetFullPath($binDir).TrimEnd('\'))
  })) {
    $env:Path = "$env:Path;$binDir"
  }

  Write-Host "Verifying Fennara CLI..."
  & $target --version

  if (Get-Command fennara -ErrorAction SilentlyContinue) {
    Write-Host "fennara command: available"
  } else {
    Write-Host "fennara command: not available in this shell yet. Open a new terminal."
  }

  Write-Host "Installed Fennara CLI to $binDir"
  Write-Host 'Run `fennara install` inside a Godot project or pass `--project <path>`.'
} finally {
  Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}
