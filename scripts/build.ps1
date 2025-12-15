[CmdletBinding()]
param(
  [ValidateSet('Debug', 'Release')]
  [string] $Configuration = 'Release',

  [ValidateSet('Win32', 'x64')]
  [string] $Platform = 'x64',

  [string] $DistDir = (Join-Path $PSScriptRoot '..' 'dist'),

  [string] $ZipName = 'DF_Korean.zip',

  [switch] $Clean
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Assert-FileExists {
  param([Parameter(Mandatory = $true)][string] $Path)
  if (-not (Test-Path -LiteralPath $Path)) {
    throw "Missing file: $Path"
  }
}

function Invoke-MsBuild {
  param(
    [Parameter(Mandatory = $true)][string] $ProjectOrSolutionPath,
    [Parameter(Mandatory = $true)][string] $OutDir
  )

  $msbuild = Get-Command msbuild -ErrorAction SilentlyContinue
  if (-not $msbuild) {
    throw 'msbuild not found. On GitHub Actions, add microsoft/setup-msbuild before running this script.'
  }

  New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
  if (-not $OutDir.EndsWith('\')) {
    $OutDir = "$OutDir\"
  }

  & $msbuild.Source $ProjectOrSolutionPath `
    /m `
    /p:Configuration=$Configuration `
    /p:Platform=$Platform `
    /p:OutDir=$OutDir `
    /verbosity:minimal
}

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$distDirResolved = (Resolve-Path -LiteralPath $DistDir -ErrorAction SilentlyContinue)
if (-not $distDirResolved) {
  New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
}
$distDirResolved = (Resolve-Path -LiteralPath $DistDir).Path

if ($Clean) {
  if (Test-Path -LiteralPath $distDirResolved) {
    Remove-Item -LiteralPath $distDirResolved -Recurse -Force
  }
  New-Item -ItemType Directory -Force -Path $distDirResolved | Out-Null
}

$buildRoot = Join-Path $distDirResolved 'build'
$stagingDir = Join-Path $distDirResolved 'staging'

New-Item -ItemType Directory -Force -Path $buildRoot, $stagingDir | Out-Null
Remove-Item -Path (Join-Path $stagingDir '*') -Recurse -Force -ErrorAction SilentlyContinue

$dfOffsetOut = Join-Path $buildRoot 'DF_kr_offset'
$launcherOut = Join-Path $buildRoot 'dfkr_launcher'
$hookOut = Join-Path $buildRoot 'Dwarf_hook_v2'

Invoke-MsBuild (Join-Path $repoRoot 'DF_kr_offset' 'DF_kr_offset.sln') $dfOffsetOut
Invoke-MsBuild (Join-Path $repoRoot 'dfkr_launcher' 'dfkr_launcher.sln') $launcherOut
Invoke-MsBuild (Join-Path $repoRoot 'Dwarf_hook_v2' 'Dwarf_hook.sln') $hookOut

$dfOffsetExe = Join-Path $dfOffsetOut 'DF_kr_offset.exe'
$launcherExe = Join-Path $launcherOut 'dfkr_launcher.exe'
$hookDll = Join-Path $hookOut 'Dwarf_hook.dll'

Assert-FileExists $dfOffsetExe
Assert-FileExists $launcherExe
Assert-FileExists $hookDll

$dataDir = Join-Path $stagingDir 'data'
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

$installBat = Join-Path $repoRoot 'scripts' 'install.bat'
Assert-FileExists $installBat
Copy-Item -LiteralPath $installBat -Destination (Join-Path $stagingDir 'install.bat') -Force

Copy-Item -LiteralPath $dfOffsetExe -Destination $dataDir -Force
Copy-Item -LiteralPath $launcherExe -Destination $dataDir -Force
Copy-Item -LiteralPath $hookDll -Destination $dataDir -Force

foreach ($asset in @('font.ttf', 'freetype.dll', 'translation_data.csv', 'translations.txt')) {
  $assetPath = Join-Path $repoRoot $asset
  Assert-FileExists $assetPath
  Copy-Item -LiteralPath $assetPath -Destination $dataDir -Force
}

$zipPath = Join-Path $distDirResolved $ZipName
if (Test-Path -LiteralPath $zipPath) {
  Remove-Item -LiteralPath $zipPath -Force
}

Push-Location $stagingDir
try {
  Compress-Archive -Path * -DestinationPath $zipPath -Force
} finally {
  Pop-Location
}

Write-Host "Created: $zipPath"
