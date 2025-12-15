@echo off

for /f %%a in ('type "%~0" ^| find /c /v ""') do set TotalLines=%%a
for /f "tokens=1,* delims=:" %%a in ('findstr /n /c:":PATCHER" "%~0"') do set StartLine=%%a
for /f "skip=%StartLine% delims=" %%i in ('type "%~0"') do (
    echo %%i >> patcher.ps1
)

echo This automatic patcher may fail depending on your system environment.
echo If an error occurs, please manually paste the patch file into the game directory.
echo:
powershell.exe -ExecutionPolicy Bypass -File patcher.ps1
pause
goto :eof

:PATCHER
Add-Type -AssemblyName System.Windows.Forms

function getLibraryFoldersVDFPath {
    $drives = Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -like "[A-Z]:\" }

    foreach ($drive in $drives) {
        $targetPath = Join-Path $drive.Root "Program Files (x86)\Steam\config\libraryfolders.vdf"

        if (Test-Path $targetPath) {
            return $targetPath
        }
    }

    return $null
}

function getSteamLibraryPaths {
    $vdfPath = getLibraryFoldersVDFPath

    if (-not $vdfPath) {
        Write-Error "Failed to retrieve the Steam library info file (libraryfolders.vdf)."
        return @()
    }

    $content = Get-Content $vdfPath -Raw
    $matches = [regex]::Matches($content, """\d+""\s+{\s+""path""\s+""([^""]+)""")

    return $matches | ForEach-Object { 
        $_.Groups[1].Value -replace '\\\\', '\'
    }
}

function getUserDFPath {
    $folderBrowser = New-Object System.Windows.Forms.FolderBrowserDialog
    $folderBrowser.Description = "Select the Dwarf Fortress folder."
    $folderBrowser.RootFolder = [System.Environment+SpecialFolder]::MyComputer
    if ($folderBrowser.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) {
        return $folderBrowser.SelectedPath
    }

    return $null
}

function getSteamDFPath {
    $libraries = getSteamLibraryPaths
    foreach ($library in $libraries) {
        $dfPath = Join-Path $library "steamapps\common\Dwarf Fortress"
        if (Test-Path $dfPath) {
            return $dfPath
        }
    }

    return $null
}

function Backup-File {
    param (
        [string]$path
    )

    $backupPath = "$path.1.bak"
    $counter = 1

    while (Test-Path -LiteralPath $backupPath) {
        $counter++
        $backupPath = "$path.$counter.bak"
    }

    Move-Item -LiteralPath $path -Destination $backupPath -Force -ErrorAction SilentlyContinue
}

function Copy-WithBackup {
    param (
        [string]$source,
        [string]$destination
    )

    $destinationDir = Split-Path -Path $destination -Parent
    if ($destinationDir -and -not (Test-Path $destinationDir)) {
        New-Item -ItemType Directory -Path $destinationDir -Force | Out-Null
    }

    if (Test-Path -LiteralPath $destination -PathType Leaf) {
        Backup-File -Path $destination
    }

    Copy-Item -LiteralPath $source -Destination $destination -Force
    $trimmedSource = $source -replace "^.+\\data", "data"
    Write-Output "$trimmedSource >>> $destination"
}

function cleanExit {
    Remove-Item -Path patcher.ps1
    exit
}

$dfPath = getSteamDFPath
if ($dfPath) {
    Write-Output "Dwarf Fortress Path (Steam): $dfPath"
} else {
    Write-Output "Please select the folder where Dwarf Fortress is installed."
    $dfPath = getUserDFPath
    if($dfPath) {
        Write-Output "Dwarf Fortress Path (User): $dfPath"
    } else {
        Write-Output "Patch cancelled."
        cleanExit
    }
}

if ($dfPath) {
    $sourceRoot = Join-Path $PSScriptRoot "data"
    $destinationRoot = $dfPath
    if ((Resolve-Path -LiteralPath $PSScriptRoot).Path -eq (Resolve-Path -LiteralPath $destinationRoot).Path) {
        Write-Host "Patch is already complete (Running patcher in game path)" -ForegroundColor Red
        cleanExit
    }
    if (Test-Path $sourceRoot) {
        if ((Resolve-Path -LiteralPath $sourceRoot).Path -eq (Resolve-Path -LiteralPath $destinationRoot).Path) {
            Write-Host "Please select the Dwarf Fortress install folder (not the patch folder: data)." -ForegroundColor Red
            cleanExit
        }
        Get-ChildItem -Path $sourceRoot -Recurse | ForEach-Object {
            $relativePath = $_.FullName.Substring($sourceRoot.Length).TrimStart('\','/')
            $destinationPath = Join-Path $destinationRoot $relativePath
            if ($_ -is [System.IO.DirectoryInfo]) {
                if (-not (Test-Path $destinationPath)) {
                    New-Item -ItemType Directory -Path $destinationPath -Force | Out-Null
                }
            } else {
                Copy-WithBackup -Source $_.FullName -Destination $destinationPath
            }
        }
        Write-Host "Korean patch done! Dwarfkkiyathou~!!!" -ForegroundColor Green
    } else {
        Write-Error "Cannot find patch folder (data). Please uncompress file first."
    }
}
cleanExit
