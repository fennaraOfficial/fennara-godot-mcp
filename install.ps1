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
$arch = switch ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture) {
  "X64" { "x86_64" }
  "Arm64" { "arm64" }
  default { throw "Unsupported architecture: $([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture)" }
}

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
$localAsset = $release.assets |
  Where-Object { $_.name -like "fennara-local-$platform-$arch-v*.zip" } |
  Select-Object -First 1
$addonAsset = $release.assets |
  Where-Object { $_.name -like "fennara-addon-$platform-$arch-v*.zip" } |
  Select-Object -First 1

if (-not $localAsset) {
  throw "Could not find fennara-local-$platform-$arch asset in release $($release.tag_name)."
}
if (-not $addonAsset) {
  throw "Could not find fennara-addon-$platform-$arch asset in release $($release.tag_name)."
}

$appDir = if ($InstallDir) { $InstallDir } else { Join-Path $env:LOCALAPPDATA "Fennara" }
$binDir = Join-Path $appDir "bin"
$versionsDir = Join-Path $appDir "versions"
$cacheDir = Join-Path $appDir "cache"
$logsDir = Join-Path $appDir "logs"
$toolsDir = Join-Path $appDir "tools"
$tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "fennara-install-$([System.Guid]::NewGuid())"
$localZipPath = Join-Path $tempDir $localAsset.name
$addonZipPath = Join-Path $tempDir $addonAsset.name
$localExtractDir = Join-Path $tempDir "local"
$addonExtractDir = Join-Path $tempDir "addon"

New-Item -ItemType Directory -Force -Path $tempDir, $localExtractDir, $addonExtractDir, $binDir, $versionsDir, $cacheDir, $logsDir, $toolsDir | Out-Null

try {
  Write-Host "Downloading $($localAsset.name)..."
  Invoke-WebRequest -Uri $localAsset.browser_download_url -OutFile $localZipPath -Headers @{ "User-Agent" = "fennara-install" }
  Expand-Archive -LiteralPath $localZipPath -DestinationPath $localExtractDir -Force

  Write-Host "Downloading $($addonAsset.name)..."
  Invoke-WebRequest -Uri $addonAsset.browser_download_url -OutFile $addonZipPath -Headers @{ "User-Agent" = "fennara-install" }
  Expand-Archive -LiteralPath $addonZipPath -DestinationPath $addonExtractDir -Force

  $versionFile = Join-Path $localExtractDir "VERSION"
  if (-not (Test-Path $versionFile)) {
    throw "Downloaded package is missing VERSION."
  }

  $packageVersion = (Get-Content -Raw $versionFile).Trim()
  if (-not $packageVersion) {
    throw "Downloaded package has an empty VERSION."
  }

  $versionDir = Join-Path $versionsDir $packageVersion
  $addonDir = Join-Path $versionDir "addon"
  New-Item -ItemType Directory -Force -Path $versionDir | Out-Null

  foreach ($name in @("fennara.exe", "fennara-mcp.exe", "fennara-daemon.exe")) {
    $source = Join-Path $localExtractDir "bin\$name"
    if (-not (Test-Path $source)) {
      throw "Downloaded package is missing $name."
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $binDir $name) -Force
  }

  foreach ($name in @("fennara-mcp-runtime.exe", "fennara-daemon-runtime.exe")) {
    $source = Join-Path $localExtractDir "bin\$name"
    if (-not (Test-Path $source)) {
      throw "Downloaded package is missing $name."
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $versionDir $name) -Force
  }

  if (Test-Path $addonDir) {
    Remove-Item -LiteralPath $addonDir -Recurse -Force
  }
  Copy-Item -LiteralPath $addonExtractDir -Destination $addonDir -Recurse -Force

  $manifest = [ordered]@{
    version = $packageVersion
    mcp_runtime = "versions/$packageVersion/fennara-mcp-runtime.exe"
    daemon_runtime = "versions/$packageVersion/fennara-daemon-runtime.exe"
    addon = "versions/$packageVersion/addon/addons/fennara"
  }
  $manifest | ConvertTo-Json | Set-Content -Encoding UTF8 -Path (Join-Path $appDir "current.json")

  $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
  $pathParts = if ($userPath) { $userPath -split ";" } else { @() }
  $hasBin = $pathParts | Where-Object {
    $_ -and ([System.IO.Path]::GetFullPath($_).TrimEnd('\') -ieq [System.IO.Path]::GetFullPath($binDir).TrimEnd('\'))
  }

  if (-not $NoModifyPath -and -not $hasBin) {
    $nextPath = if ($userPath) { "$userPath;$binDir" } else { $binDir }
    [Environment]::SetEnvironmentVariable("Path", $nextPath, "User")
    if (-not (($env:Path -split ";") | Where-Object {
      $_ -and ([System.IO.Path]::GetFullPath($_).TrimEnd('\') -ieq [System.IO.Path]::GetFullPath($binDir).TrimEnd('\'))
    })) {
      $env:Path = "$env:Path;$binDir"
    }
    Write-Host "Added $binDir to your user PATH. Open a new terminal before running fennara by name."
  } elseif ($NoModifyPath) {
    Write-Host "Skipped PATH update. Add this directory to PATH when you want to run fennara by name:"
    Write-Host "  $binDir"
  }

  Write-Host "Installed Fennara $packageVersion to $appDir"
  & (Join-Path $binDir "fennara.exe") doctor --repair
} finally {
  Remove-Item -LiteralPath $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}
