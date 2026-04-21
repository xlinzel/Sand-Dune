param(
    [ValidateSet("x64-windows", "x86-windows")]
    [string]$Triplet = "x64-windows",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$vcpkgRoot = Join-Path $repoRoot ".deps\vcpkg"
$vcpkgBaseline = "1e199d32ad53aab1defda61ce41c380302e3f95c"

function Find-ExistingPath {
    param([string[]]$Candidates)

    foreach ($candidate in $Candidates) {
        if ($candidate -and (Test-Path $candidate)) {
            return $candidate
        }
    }

    return $null
}

function Invoke-CheckedCommand {
    param(
        [string]$Description,
        [string]$FilePath,
        [string[]]$Arguments = @()
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

function Resolve-CommandPath {
    param(
        [string]$Name,
        [string[]]$Fallbacks = @()
    )

    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) {
        return $command.Source
    }

    $fallback = Find-ExistingPath $Fallbacks
    if ($fallback) {
        return $fallback
    }

    throw "Required command '$Name' was not found. Install it or make sure it is available to this shell."
}

function Clear-BrokenLoopbackProxy {
    $proxyVariables = @("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy")
    $clearedAny = $false

    foreach ($variable in $proxyVariables) {
        $value = [Environment]::GetEnvironmentVariable($variable, "Process")
        if ($value -match '^(http|https)://(127\.0\.0\.1|localhost):9/?$') {
            [Environment]::SetEnvironmentVariable($variable, $null, "Process")
            $clearedAny = $true
        }
    }

    if ($clearedAny) {
        Write-Host "Cleared loopback proxy variables that would block dependency downloads." -ForegroundColor Yellow
    }
}

function Resolve-VisualStudioInstallation {
    $vswhere = Find-ExistingPath @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe")
    )

    if ($vswhere) {
        $installationPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
        if ($installationPath) {
            return $installationPath.Trim()
        }
    }

    $visualStudioRoot = Join-Path $env:ProgramFiles "Microsoft Visual Studio"
    if (Test-Path $visualStudioRoot) {
        $majorVersions = Get-ChildItem $visualStudioRoot -Directory -ErrorAction SilentlyContinue
        foreach ($majorVersion in $majorVersions) {
            $editions = Get-ChildItem $majorVersion.FullName -Directory -ErrorAction SilentlyContinue
            foreach ($edition in $editions) {
                if (Test-Path (Join-Path $edition.FullName "Common7\Tools\VsDevCmd.bat")) {
                    return $edition.FullName
                }
            }
        }
    }

    throw "Could not locate a Visual Studio installation with the MSVC C++ toolchain."
}

function Import-VisualStudioEnvironment {
    param([string]$TargetArch)

    $installationPath = Resolve-VisualStudioInstallation
    $vsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"

    if (-not (Test-Path $vsDevCmd)) {
        throw "Could not find VsDevCmd.bat at $vsDevCmd"
    }

    Write-Host "Loading MSVC environment for $TargetArch" -ForegroundColor Cyan
    $command = "`"$vsDevCmd`" -no_logo -host_arch=x64 -arch=$TargetArch >nul && set"
    $environmentDump = & cmd /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to load the Visual Studio developer environment."
    }

    foreach ($line in $environmentDump) {
        if ($line -match "^(.*?)=(.*)$") {
            Set-Item -Path ("Env:{0}" -f $Matches[1]) -Value $Matches[2]
        }
    }

    return $installationPath
}

$targetArch = if ($Triplet -eq "x64-windows") { "x64" } else { "x86" }
$vsInstall = Import-VisualStudioEnvironment -TargetArch $targetArch
Clear-BrokenLoopbackProxy

$gitExe = Resolve-CommandPath "git" @(
    (Join-Path $env:LocalAppData "Programs\Git\cmd\git.exe"),
    (Join-Path $env:ProgramFiles "Git\cmd\git.exe"),
    (Join-Path ${env:ProgramFiles(x86)} "Git\cmd\git.exe")
)

$cmakeExe = Resolve-CommandPath "cmake" @(
    (Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe")
)

$ninjaExe = Resolve-CommandPath "ninja" @(
    (Join-Path $vsInstall "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe")
)

$env:PATH = "{0};{1};{2}" -f (Split-Path -Parent $cmakeExe), (Split-Path -Parent $ninjaExe), $env:PATH

$configurePreset = "windows-{0}-{1}" -f ($Triplet -replace "-windows", ""), $Configuration.ToLowerInvariant()
$buildPreset = "build-{0}" -f $configurePreset
$binaryDir = Join-Path $repoRoot ("build\{0}" -f $configurePreset)
$cachePath = Join-Path $binaryDir "CMakeCache.txt"

if (-not (Test-Path $cachePath)) {
    if (-not (Test-Path $vcpkgRoot)) {
        Write-Host "Cloning vcpkg into $vcpkgRoot" -ForegroundColor Cyan
        Invoke-CheckedCommand "Cloning vcpkg" $gitExe @("clone", "https://github.com/microsoft/vcpkg", $vcpkgRoot)
    }

    & $gitExe -C $vcpkgRoot cat-file -e "$vcpkgBaseline`^{commit}" 2>$null
    $baselinePresent = ($LASTEXITCODE -eq 0)

    if (-not $baselinePresent) {
        Write-Host "Fetching vcpkg history to locate baseline $vcpkgBaseline" -ForegroundColor Cyan
        Invoke-CheckedCommand "Fetching vcpkg history" $gitExe @("-C", $vcpkgRoot, "fetch", "--all", "--tags", "--prune")
    }

    Write-Host "Checking out vcpkg baseline $vcpkgBaseline" -ForegroundColor Cyan
    Invoke-CheckedCommand "Checking out vcpkg baseline" $gitExe @("-C", $vcpkgRoot, "checkout", $vcpkgBaseline)

    $bootstrapScript = Join-Path $vcpkgRoot "bootstrap-vcpkg.bat"
    $vcpkgExe = Join-Path $vcpkgRoot "vcpkg.exe"
    if (-not (Test-Path $bootstrapScript)) {
        throw "Could not find bootstrap script at $bootstrapScript"
    }

    if (-not (Test-Path $vcpkgExe)) {
        Write-Host "Bootstrapping vcpkg" -ForegroundColor Cyan
        Invoke-CheckedCommand "Bootstrapping vcpkg" $bootstrapScript @("-disableMetrics")
    } else {
        Write-Host "Using existing vcpkg executable at $vcpkgExe" -ForegroundColor Cyan
    }

    Push-Location $repoRoot

    Write-Host "Configuring with preset $configurePreset" -ForegroundColor Cyan
    Invoke-CheckedCommand "Configuring CMake" $cmakeExe @("--preset", $configurePreset)

    Pop-Location
} else {
    Write-Host "Using existing configure at $binaryDir" -ForegroundColor Cyan
}

Push-Location $repoRoot

if (-not $NoBuild) {
    Write-Host "Building with preset $buildPreset" -ForegroundColor Cyan
    Invoke-CheckedCommand "Building project" $cmakeExe @("--build", "--preset", $buildPreset)
}

Pop-Location

Write-Host "Done." -ForegroundColor Green
Write-Host "Build directory: $binaryDir" -ForegroundColor Green

if (-not $NoBuild) {
    $appPath = Join-Path $binaryDir "sand_dune.exe"
    if (Test-Path $appPath) {
        Write-Host "Application: $appPath" -ForegroundColor Green
    }
}
