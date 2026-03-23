param(
  [string]$DevEcoHome = $env:DEVECO_STUDIO_HOME,
  [switch]$NoDaemon = $true
)

if ([string]::IsNullOrWhiteSpace($DevEcoHome)) {
  $DevEcoHome = 'D:\Develop\IDE\DevEcoStudio\DevEco Studio'
}

$hvigorPath = Join-Path $DevEcoHome 'tools\hvigor\hvigor\bin\hvigor.js'
$javaHome = Join-Path $DevEcoHome 'jbr'
$javaExe = Join-Path $javaHome 'bin\java.exe'
$sdkHome = Join-Path $DevEcoHome 'sdk'

if (-not (Test-Path $hvigorPath)) {
  throw "hvigor.js not found: $hvigorPath. Set DEVECO_STUDIO_HOME or pass -DevEcoHome."
}

if (-not (Test-Path $javaExe)) {
  throw "java.exe not found: $javaExe. Set DEVECO_STUDIO_HOME or pass -DevEcoHome."
}

$env:DEVECO_SDK_HOME = $sdkHome
$env:JAVA_HOME = $javaHome
$env:PATH = "$($javaHome)\bin;$env:PATH"

$hvigorArgs = @(
  '--mode', 'module',
  '-p', 'product=default',
  '-p', 'buildMode=debug',
  '-p', 'module=entry@default',
  '-p', 'apiType=stage',
  'assembleHap'
)

if ($NoDaemon) {
  $hvigorArgs += '--no-daemon'
}

node $hvigorPath @hvigorArgs
exit $LASTEXITCODE
