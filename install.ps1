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
  irm https://raw.githubusercontent.com/fennaraOfficial/fennara-godot-ai/main/install.ps1 | iex
  ./install.ps1 -Version 0.2.8

Options:
  -Version <version>     Install a specific version without v prefix. Default: latest
  -InstallDir <path>     Install directory. Default: %LOCALAPPDATA%\Fennara
  -NoModifyPath          Do not add the Fennara bin directory to user PATH
  -Help                  Show this help
"@
  exit 0
}

$repo = "fennaraOfficial/fennara-godot-ai"
$platform = "windows"
$arch = "x86_64"
$networkTimeoutSeconds = 120

function Format-Bytes([long] $Bytes) {
  if ($Bytes -ge 1MB) {
    return "{0:n1} MB" -f ($Bytes / 1MB)
  }
  if ($Bytes -ge 1KB) {
    return "{0:n1} KB" -f ($Bytes / 1KB)
  }
  return "$Bytes B"
}

function Show-MissingRuntimeHelp {
  Write-Host ""
  Write-Host "Your Fennara CLI installed, but Windows cannot start it."
  Write-Host "The exit code -1073741515 means a required Windows DLL is missing."
  Write-Host "Please install Microsoft Visual C++ Redistributable 2015-2022 x64, then reopen PowerShell and run:"
  Write-Host ""
  Write-Host "  fennara --version"
  Write-Host "  fennara doctor"
  Write-Host "  fennara install"
  Write-Host ""
  Write-Host "Download:"
  Write-Host "https://aka.ms/vs/17/release/vc_redist.x64.exe"
  Write-Host ""
  Write-Host "Expected first line:"
  Write-Host "  fennara <version>"
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
Write-Host "release api: $releaseApi"
$release = Invoke-RestMethod -Uri $releaseApi -Headers @{ "User-Agent" = "fennara-install" } -TimeoutSec $networkTimeoutSeconds
Write-Host "release: $($release.tag_name)"
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
  Write-Host "from: $($asset.browser_download_url)"
  Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zipPath -Headers @{ "User-Agent" = "fennara-install" } -TimeoutSec $networkTimeoutSeconds
  $download = Get-Item -LiteralPath $zipPath
  Write-Host "downloaded: $($asset.name) ($(Format-Bytes $download.Length))"
  Write-Host "Extracting Fennara CLI..."
  Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir -Force

  $source = Join-Path $extractDir "bin\fennara.exe"
  if (-not (Test-Path $source)) {
    throw "Downloaded package is missing fennara.exe."
  }

  $target = Join-Path $binDir "fennara.exe"
  Write-Host "Installing Fennara CLI to $target..."
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
  $versionOutput = & $target --version 2>&1
  $verifyExitCode = $LASTEXITCODE
  if ($verifyExitCode -ne 0) {
    if (($verifyExitCode -eq -1073741515) -or ("$verifyExitCode" -eq "3221225781")) {
      Show-MissingRuntimeHelp
    }
    if ($versionOutput) {
      Write-Host "verification output:"
      $versionOutput | ForEach-Object { Write-Host "  $_" }
    }
    throw "Fennara CLI verification failed with exit code $verifyExitCode."
  }
  $versionOutput | ForEach-Object { Write-Host $_ }

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
