param(
  [string]$DevEcoHome = $env:DEVECO_STUDIO_HOME,
  [string]$WorkspaceRoot = '',
  [string]$Target = '',
  [switch]$SkipInstall
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$Message) {
  Write-Host "[Aquarium_APP] $Message"
}

function Get-PathMd5([string]$Text) {
  $md5 = [System.Security.Cryptography.MD5]::Create()
  try {
    $bytes = [System.Text.Encoding]::UTF8.GetBytes($Text)
    return (($md5.ComputeHash($bytes) | ForEach-Object ToString x2) -join '')
  } finally {
    $md5.Dispose()
  }
}

function Invoke-Robocopy([string]$Source, [string]$Destination, [string[]]$ExtraArgs) {
  $args = @(
    $Source,
    $Destination,
    '/MIR',
    '/XD', 'oh_modules', '.hvigor', '.idea', 'build', 'entry\build',
    '/XF', 'local.properties', 'hvigor_build.log', 'hvigor_build_latest.log', 'hvigor_build_stack.log'
  ) + $ExtraArgs

  & robocopy @args | Out-Null
  if ($LASTEXITCODE -gt 7) {
    throw "robocopy failed, exit code: $LASTEXITCODE"
  }
}

function Ensure-Junction([string]$LinkPath, [string]$TargetPath) {
  if (Test-Path $LinkPath) {
    return
  }
  $parent = Split-Path -Parent $LinkPath
  if (-not (Test-Path $parent)) {
    New-Item -ItemType Directory -Force -Path $parent | Out-Null
  }
  cmd /c "mklink /J `"$LinkPath`" `"$TargetPath`"" | Out-Null
}

function Ensure-HvigorCache([string]$ProjectPath, [string]$DevEcoRoot) {
  $hash = Get-PathMd5($ProjectPath)
  $cacheRoot = Join-Path $env:USERPROFILE ".hvigor\project_caches\$hash\workspace\node_modules\@ohos"
  if (-not (Test-Path $cacheRoot)) {
    New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
  }

  Ensure-Junction (Join-Path $cacheRoot 'hvigor') (Join-Path $DevEcoRoot 'tools\hvigor\hvigor')
  Ensure-Junction (Join-Path $cacheRoot 'hvigor-ohos-plugin') (Join-Path $DevEcoRoot 'tools\hvigor\hvigor-ohos-plugin')
}

function Ensure-ConnectedTarget([string]$HdcExe, [string]$TargetKey) {
  if ([string]::IsNullOrWhiteSpace($TargetKey)) {
    return
  }
  & $HdcExe tconn $TargetKey | Out-Null
}

if ([string]::IsNullOrWhiteSpace($DevEcoHome)) {
  $DevEcoHome = 'D:\Develop\IDE\DevEcoStudio\DevEco Studio'
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourceRoot = Join-Path $repoRoot 'Aquarium_APP'
if (-not (Test-Path $sourceRoot)) {
  throw "Aquarium_APP source project was not found: $sourceRoot"
}

if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  $WorkspaceRoot = Join-Path $env:LOCALAPPDATA 'stm32Aquarium\Aquarium_APP_ntfs'
}

$ohpmExe = Join-Path $DevEcoHome 'tools\ohpm\bin\ohpm.bat'
$hdcExe = Join-Path $DevEcoHome 'sdk\default\openharmony\toolchains\hdc.exe'
$buildScript = Join-Path $WorkspaceRoot 'scripts\build_api17.ps1'
$releaseRoot = Join-Path $repoRoot 'release\Aquarium_APP'
$signedHap = Join-Path $WorkspaceRoot 'entry\build\default\outputs\default\entry-default-signed.hap'
$unsignedHap = Join-Path $WorkspaceRoot 'entry\build\default\outputs\default\entry-default-unsigned.hap'

Write-Step "Prepare NTFS workspace: $WorkspaceRoot"
if (-not (Test-Path $WorkspaceRoot)) {
  New-Item -ItemType Directory -Force -Path $WorkspaceRoot | Out-Null
}

Write-Step 'Sync Aquarium_APP into workspace'
Invoke-Robocopy $sourceRoot $WorkspaceRoot @()

Write-Step 'Ensure Hvigor project cache'
Ensure-HvigorCache $WorkspaceRoot $DevEcoHome

Push-Location $WorkspaceRoot
try {
  if (-not $SkipInstall) {
    Write-Step 'Run ohpm install'
    & $ohpmExe install | Out-Host
    if ($LASTEXITCODE -ne 0) {
      throw "ohpm install failed, exit code: $LASTEXITCODE"
    }
  }

  Write-Step 'Build HAP package'
  & powershell -ExecutionPolicy Bypass -File $buildScript | Out-Host
  if ($LASTEXITCODE -ne 0) {
    throw "HAP build failed, exit code: $LASTEXITCODE"
  }
} finally {
  Pop-Location
}

Write-Step "Copy artifacts back to repo: $releaseRoot"
if (-not (Test-Path $releaseRoot)) {
  New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
}
Copy-Item $signedHap (Join-Path $releaseRoot 'entry-default-signed.hap') -Force
Copy-Item $unsignedHap (Join-Path $releaseRoot 'entry-default-unsigned.hap') -Force
Copy-Item (Join-Path $WorkspaceRoot 'entry\build\default\outputs\default\pack.info') (Join-Path $releaseRoot 'pack.info') -Force

if (-not [string]::IsNullOrWhiteSpace($Target)) {
  Write-Step "Connect target: $Target"
  Ensure-ConnectedTarget $hdcExe $Target
  Write-Step 'Install signed package'
  & $hdcExe -t $Target install -r (Join-Path $releaseRoot 'entry-default-signed.hap') | Out-Host
  if ($LASTEXITCODE -ne 0) {
    throw "HAP install failed, exit code: $LASTEXITCODE"
  }
  Write-Step 'Start application'
  & $hdcExe -t $Target shell aa start -b com.hsiangpo.AquariumAPP -m entry -a EntryAbility | Out-Host
}

Write-Step "Done. Signed package: $releaseRoot\entry-default-signed.hap"
