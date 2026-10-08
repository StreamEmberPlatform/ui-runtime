#Requires -Version 5.1
<#
.SYNOPSIS
    StreamEmber Overlay (ui-runtime): build, package and (optionally) install, for GTA V and RDR2.
    The overlay is an engine only: it ships no page, trainer or UI kit. Scripts load their page (usually from a CDN)
    with OverlayBridge.LoadUrl.

.DESCRIPTION
    1. Version: VERSION (major.minor) + commits since it changed = patch (tools/StreamEmber.Build.psm1).
    2. CEF (minimal distribution) into third_party\cef, pinned by cef.lock (SHA-1 checked). -UpdateCef moves to the
       newest stable build in the supported range and rewrites cef.lock.
    3. C++ (CMake + Visual Studio): core, CEF host, GTA V and RDR2 backends, with version resources. The ScriptHookV
       SDK subset and MinHook are in vendor\.
    4. C#: StreamEmber.Overlay.Bridge (game independent; scripts reference it).
    5. dist\<GAME>\ = the game-folder layout, then artifacts\StreamEmber.Overlay.<GAME>-<version>.zip (+ .sha256):
         StreamEmber.Overlay.<GAME>.asi
         StreamEmber\Overlay\            core, CEF host and CEF files
         StreamEmber\Scripts\            StreamEmber.Overlay.Bridge.dll
         StreamEmber\Config\Overlay.ini
         StreamEmber\Licenses\StreamEmber.Overlay.<GAME>\
         StreamEmber\Manifests\StreamEmber.Overlay.<GAME>.json
    6. -Deploy: copies dist\<GAME> into the game folder (keeps Overlay.ini, disables files of the old layout).

    Needs: Visual Studio 2022+ ("Desktop development with C++"), CMake 3.21+ (the one in Visual Studio works),
    .NET SDK. Internet: cef-builds.spotifycdn.com (CEF, pinned by cef.lock and SHA-1 checked).

.EXAMPLE
    .\build.ps1
.EXAMPLE
    .\build.ps1 -Game GTAV -Deploy -GamePath "D:\EpicGames\GTAV"
.EXAMPLE
    .\build.ps1 -Game RDR2 -Deploy -GamePath "D:\SteamLibrary\steamapps\common\Red Dead Redemption 2"
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    # Explicit product version (CI passes the computed one); default: computed, with a -dev suffix
    [string]$Version = '',
    # Packages to build. -Deploy needs a single game.
    [ValidateSet('All', 'GTAV', 'RDR2')]
    [string]$Game = 'All',
    [switch]$UpdateCef,
    [switch]$Deploy,
    # Game folder (GTA5.exe / RDR2.exe). Default: GTAV_GAME_PATH / RDR2_GAME_PATH environment variable
    [string]$GamePath = '',
    # Overwrite the game's Overlay.ini with the template
    [switch]$ResetConfig
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$Root = $PSScriptRoot
Import-Module (Join-Path $Root 'tools\StreamEmber.Build.psm1') -Force

$ThirdParty = Join-Path $Root 'third_party'
$CefDir = Join-Path $ThirdParty 'cef'
$CefMarker = Join-Path $CefDir '.streamember-cef-version'
$CefLock = Join-Path $Root 'cef.lock'
$BuildDir = Join-Path $Root 'build'
$DistDir = Join-Path $Root 'dist'
$ArtifactsDir = Join-Path $Root 'artifacts'
$CefIndexUrl = 'https://cef-builds.spotifycdn.com/index.json'
$CefCdn = 'https://cef-builds.spotifycdn.com/'
# CEF major versions the code is checked against (same as the Chromium major version)
$CefMajorMin = 152
$CefMajorMax = 156

$Games = [ordered]@{
    GTAV = @{
        Name = 'StreamEmber Overlay (GTA V)'; Exe = 'GTA5.exe'; Process = 'GTA5'; Env = 'GTAV_GAME_PATH'
        # Files of the old layout (OverlayRuntime builds before the StreamEmber layout)
        Conflicts = @('scripts/StreamEmber.Overlay.Bridge.dll', 'scripts/StreamEmber.TrainerDemo.dll',
                      'scripts/OverlayDemo.3.cs', 'StreamEmber/Overlay/overlay.ini', 'StreamEmber/Overlay/ui')
        Requires = @([ordered]@{ file = 'ScriptHookV.dll'; name = 'Script Hook V (Alexander Blade)'; url = 'http://www.dev-c.com/gtav/scripthookv/' })
    }
    RDR2 = @{
        Name = 'StreamEmber Overlay (RDR2)'; Exe = 'RDR2.exe'; Process = 'RDR2'; Env = 'RDR2_GAME_PATH'
        Conflicts = @('scripts/StreamEmber.Overlay.Bridge.dll', 'scripts/StreamEmber.Rdr2TrainerDemo.dll',
                      'StreamEmber/Overlay/overlay.ini', 'StreamEmber/Overlay/ui')
        Requires = @([ordered]@{ file = 'ScriptHookRDR2.dll'; name = 'Script Hook RDR2 (Alexander Blade)'; url = 'http://www.dev-c.com/rdr2/scripthookrdr2/' },
                     [ordered]@{ file = 'dinput8.dll'; name = 'ASI Loader (Script Hook RDR2 package)'; url = 'http://www.dev-c.com/rdr2/scripthookrdr2/' })
    }
}
$Preserve = @('StreamEmber/Config/Overlay.ini')
$Selected = if ($Game -eq 'All') { @($Games.Keys) } else { @($Game) }
if ($Deploy -and $Game -eq 'All') { throw '-Deploy needs a single game: -Game GTAV or -Game RDR2.' }

function Write-Title([string]$Text) { Write-Host ''; Write-Host $Text -ForegroundColor Cyan }
function Write-Ok([string]$Text) { Write-Host "  [OK] $Text" -ForegroundColor Green }

# --- CEF --------------------------------------------------------------------------------------------------------
function Get-CefVersionKey([string]$CefVersion) {
    # "154.0.7+gabc1234+chromium-154.0.8037.98" -> [version]154.0.7
    try { return [version]$CefVersion.Split('+')[0] } catch { return [version]'0.0' }
}

function Resolve-CefBuild {
    Write-Host "  Reading $CefIndexUrl"
    $index = Invoke-RestMethod -Uri $CefIndexUrl -UseBasicParsing
    $candidates = @($index.windows64.versions | Where-Object {
        $major = (Get-CefVersionKey $_.cef_version).Major
        $_.channel -eq 'stable' -and $major -ge $CefMajorMin -and $major -le $CefMajorMax
    } | Sort-Object -Property @{ Expression = { Get-CefVersionKey $_.cef_version } } -Descending)
    if ($candidates.Count -eq 0) { throw "No stable CEF build in the supported range ($CefMajorMin-$CefMajorMax)." }
    $pick = $candidates[0]
    $file = @($pick.files | Where-Object { $_.type -eq 'minimal' })[0]
    if (-not $file) { throw "CEF $($pick.cef_version) has no minimal distribution." }
    return [pscustomobject]@{ version = $pick.cef_version; chromium = $pick.chromium_version; name = $file.name; sha1 = $file.sha1 }
}

function Expand-TarBz2([string]$Archive, [string]$Destination) {
    $tar = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($tar) {
        & $tar.Source -xjf $Archive -C $Destination | Out-Host
        if ($LASTEXITCODE -eq 0) { return }
        Write-Warning 'tar.exe could not extract the archive; trying 7-Zip.'
    }
    $sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
    $path7z = if ($sevenZip) { $sevenZip.Source } else { Join-Path $env:ProgramFiles '7-Zip\7z.exe' }
    if (-not (Test-Path $path7z)) { throw 'Cannot extract tar.bz2: needs Windows tar.exe or 7-Zip.' }
    & $path7z x $Archive "-o$Destination" -y | Out-Null
    $inner = Join-Path $Destination ([IO.Path]::GetFileNameWithoutExtension($Archive))
    & $path7z x $inner "-o$Destination" -y | Out-Null
    Remove-Item $inner -Force
}

function Install-Cef {
    Write-Title 'CEF'
    if ((Test-Path $CefLock) -and -not $UpdateCef) {
        $lock = Get-Content $CefLock -Raw | ConvertFrom-Json
    } else {
        $lock = Resolve-CefBuild
        [IO.File]::WriteAllText($CefLock, ($lock | ConvertTo-Json) + "`n", (New-Object Text.UTF8Encoding($false)))
        Write-Ok "cef.lock: $($lock.version)"
    }
    if ((Test-Path $CefMarker) -and ((Get-Content $CefMarker -Raw).Trim() -eq $lock.version)) {
        Write-Ok "CEF $($lock.version)"
        return $lock
    }

    New-Item -ItemType Directory -Force -Path $ThirdParty | Out-Null
    $archive = Join-Path $ThirdParty $lock.name
    $url = $CefCdn + [Uri]::EscapeDataString($lock.name)
    if (-not (Test-Path $archive)) {
        Write-Host "  Downloading $($lock.name)"
        $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
        if ($curl) {
            & $curl.Source -L --fail --retry 3 -o $archive $url | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "CEF download failed (curl $LASTEXITCODE)." }
        } else {
            Invoke-WebRequest -Uri $url -OutFile $archive -UseBasicParsing
        }
    }
    $hash = (Get-FileHash -Path $archive -Algorithm SHA1).Hash.ToLowerInvariant()
    if ($hash -ne $lock.sha1.ToLowerInvariant()) {
        Remove-Item $archive -Force
        throw "CEF archive SHA-1 mismatch (expected $($lock.sha1), got $hash)."
    }
    Write-Host '  Extracting'
    if (Test-Path $CefDir) { Remove-Item $CefDir -Recurse -Force }
    Expand-TarBz2 $archive $ThirdParty
    $extracted = Join-Path $ThirdParty ($lock.name -replace '\.tar\.bz2$', '')
    if (-not (Test-Path $extracted)) { throw "Expected folder missing: $extracted" }
    Rename-Item $extracted $CefDir
    Set-Content -Path $CefMarker -Value $lock.version -Encoding ASCII
    Remove-Item $archive -Force
    Write-Ok "CEF $($lock.version) (Chromium $($lock.chromium))"
    return $lock
}

# --- C++ --------------------------------------------------------------------------------------------------------
function Find-CMake {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -prerelease -products * -property installationPath
        $candidate = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $candidate) { return $candidate }
    }
    throw 'cmake.exe not found. Install the Visual Studio "C++ CMake tools" component or CMake 3.21+.'
}

function Find-Output([string]$Name) {
    $file = Get-ChildItem -Path $BuildDir -Recurse -File -Filter $Name |
        Where-Object { $_.DirectoryName -match "[\\/]$Configuration$" } | Select-Object -First 1
    if (-not $file) { throw "Build output not found: $Name" }
    return $file.FullName
}

function Build-Native {
    Write-Title 'C++ (CMake + Visual Studio)'
    $cmake = Find-CMake
    & $cmake -S $Root -B $BuildDir -A x64 "-DCEF_ROOT=$CefDir" "-DSE_VERSION=$Version" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed ($LASTEXITCODE)." }
    & $cmake --build $BuildDir --config $Configuration --parallel | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "C++ build failed ($LASTEXITCODE)." }
    Write-Ok 'Core, host and backends built.'
}

# --- C# ---------------------------------------------------------------------------------------------------------
function Build-Bridge {
    Write-Title 'C# (StreamEmber.Overlay.Bridge)'
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if (-not $dotnet) { throw '.NET SDK not found (dotnet).' }
    $out = Join-Path $BuildDir 'managed'
    if (Test-Path $out) { Remove-Item $out -Recurse -Force }
    & $dotnet.Source build (Join-Path $Root 'bridge\StreamEmber.Overlay.Bridge\StreamEmber.Overlay.Bridge.csproj') `
        -c $Configuration -o $out --nologo -v minimal "-p:SE_VERSION=$Version" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Bridge build failed ($LASTEXITCODE)." }
    Write-Ok 'StreamEmber.Overlay.Bridge.dll'
    return $out
}

# --- Package ----------------------------------------------------------------------------------------------------
function New-GamePackage([string]$Name, [string]$ManagedDir) {
    $info = $Games[$Name]
    $id = "StreamEmber.Overlay.$Name"
    $stage = Join-Path $DistDir $Name
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    $overlayDir = Join-Path $stage 'StreamEmber\Overlay'
    $scriptsDir = Join-Path $stage 'StreamEmber\Scripts'
    $configDir = Join-Path $stage 'StreamEmber\Config'
    $licenseDir = Join-Path $stage "StreamEmber\Licenses\$id"
    New-Item -ItemType Directory -Force -Path $overlayDir, $scriptsDir, $configDir, $licenseDir | Out-Null

    Copy-Item (Find-Output "$id.asi") $stage
    Copy-Item (Find-Output 'StreamEmber.Overlay.dll') $overlayDir
    Copy-Item (Find-Output 'StreamEmber.Overlay.Host.exe') $overlayDir
    # CEF runtime files: Release\ (dll, bin, json) + Resources\ (pak, icudtl.dat, locales\)
    Get-ChildItem (Join-Path $CefDir 'Release') -File | Where-Object { $_.Extension -in '.dll', '.bin', '.json' } |
        ForEach-Object { Copy-Item $_.FullName $overlayDir }
    Copy-Item (Join-Path $CefDir 'Resources\*') $overlayDir -Recurse -Force

    Copy-Item (Join-Path $ManagedDir 'StreamEmber.Overlay.Bridge.dll') $scriptsDir
    Copy-Item (Join-Path $Root 'package\Config\Overlay.ini') $configDir

    Copy-Item (Join-Path $Root 'THIRD-PARTY-NOTICES.md') (Join-Path $licenseDir 'THIRD-PARTY-NOTICES.txt')
    Copy-Item (Join-Path $CefDir 'LICENSE.txt') (Join-Path $licenseDir 'CEF.LICENSE.txt')
    if ($Name -eq 'RDR2') {
        Copy-Item (Join-Path $Root 'vendor\minhook\LICENSE.txt') (Join-Path $licenseDir 'MinHook.LICENSE.txt')
    }

    New-SEManifest -StageDirectory $stage -Id $id -Name $info.Name -Version $Version -Game $Name `
        -Preserve $Preserve -Conflicts $info.Conflicts -Requires $info.Requires -RepositoryRoot $Root | Out-Null
    $zip = New-SEPackage -StageDirectory $stage -OutputDirectory $ArtifactsDir -Id $id -Version $Version
    Write-Ok ("{0} ({1:N0} MB)" -f (Split-Path $zip -Leaf), ((Get-Item $zip).Length / 1MB))
    return $stage
}

# --- Install ----------------------------------------------------------------------------------------------------
function Install-ToGame([string]$Name, [string]$Stage) {
    Write-Title "Install ($Name)"
    $info = $Games[$Name]
    $gameDir = if ($GamePath) { $GamePath } else { [Environment]::GetEnvironmentVariable($info.Env) }
    if (-not $gameDir) { throw "Game folder unknown: pass -GamePath or set $($info.Env)." }
    if (-not (Test-Path (Join-Path $gameDir $info.Exe))) { throw "$($info.Exe) not found in $gameDir" }

    # Old layout: StreamEmber\Overlay\overlay.ini -> StreamEmber\Config\Overlay.ini (user settings carry over)
    $oldIni = Join-Path $gameDir 'StreamEmber\Overlay\overlay.ini'
    $newIni = Join-Path $gameDir 'StreamEmber\Config\Overlay.ini'
    if ((Test-Path $oldIni) -and -not (Test-Path $newIni) -and -not $ResetConfig) {
        New-Item -ItemType Directory -Force -Path (Split-Path $newIni) | Out-Null
        Move-Item $oldIni $newIni
        Write-Host '  moved StreamEmber\Overlay\overlay.ini -> StreamEmber\Config\Overlay.ini'
    }
    Install-SEPackage -StageDirectory $Stage -GameDirectory $gameDir -GameExecutable $info.Exe -ProcessName $info.Process `
        -Preserve $Preserve -Conflicts $info.Conflicts -ResetConfig:$ResetConfig
    foreach ($need in $info.Requires) {
        if (-not (Test-Path (Join-Path $gameDir $need.file))) { Write-Warning "$($need.file) missing in the game folder ($($need.name))." }
    }
    Write-Ok "Installed into $gameDir (logs: StreamEmber\Logs\Overlay*.log)"
}

# --- Main -------------------------------------------------------------------------------------------------------
if (-not $Version) { $Version = Get-SEVersion -RepositoryRoot $Root -Kind Dev }
Write-Host "StreamEmber Overlay $Version ($($Selected -join ', '))" -ForegroundColor Cyan

$cef = Install-Cef
Build-Native
$managed = Build-Bridge

Write-Title 'Packages'
$stages = @{}
foreach ($name in $Selected) { $stages[$name] = New-GamePackage $name $managed }

if ($Deploy) { Install-ToGame $Game $stages[$Game] }
Write-Host ''
Write-Ok "StreamEmber Overlay $Version (CEF $($cef.version), $Configuration)"
