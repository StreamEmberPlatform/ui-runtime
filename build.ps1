#Requires -Version 5.1
<#
.SYNOPSIS
    StreamEmber Overlay (ui-runtime): build, package and (optionally) install, for GTA V and RDR2.

.DESCRIPTION
    1. Version: VERSION (major.minor) + commits since it changed = patch (tools/StreamEmber.Build.psm1).
    2. CEF (minimal distribution) into third_party\cef, pinned by cef.lock (SHA-1 checked). -UpdateCef moves to the
       newest stable build in the supported range and rewrites cef.lock.
    3. MHud (the UI kit, StreamEmberPlatform/mhud), pinned by mhud.lock. Source, in order: -MHudPath, MHUD_PATH,
       the sibling checkout ..\mhud (CI checks the pinned commit out and passes -MHudPath).
    4. C++ (CMake + Visual Studio): core, CEF host, GTA V and RDR2 backends, with version resources.
    5. C#: StreamEmber.Overlay.Bridge + StreamEmber.Trainer.<GAME>, compiled against StreamEmber.Scripting.<GAME>.dll.
       Source, in order: -ScriptingDir, the sibling runtime builds ..\gtav-runtime-scripthook\bin\Release and
       ..\rdr2-runtime-scripthook\bin\Release (CI downloads the latest runtime releases and passes -ScriptingDir).
    6. dist\<GAME>\ = the game-folder layout, then artifacts\StreamEmber.Overlay.<GAME>-<version>.zip (+ .sha256):
         StreamEmber.Overlay.<GAME>.asi
         StreamEmber\Overlay\            core, CEF host and CEF files, ui\ (test page, trainer, MHud)
         StreamEmber\Scripts\            StreamEmber.Overlay.Bridge.dll, StreamEmber.Trainer.<GAME>.dll
         StreamEmber\Config\Overlay.ini
         StreamEmber\Licenses\StreamEmber.Overlay.<GAME>\
         StreamEmber\Manifests\StreamEmber.Overlay.<GAME>.json
    7. -Deploy: copies dist\<GAME> into the game folder (keeps Overlay.ini, disables files of the old layout).

    Needs: Visual Studio 2022+ ("Desktop development with C++"), CMake 3.21+ (the one in Visual Studio works),
    .NET SDK. Internet: cef-builds.spotifycdn.com, github.com (MinHook).

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
    # MHud checkout (repository root)
    [string]$MHudPath = '',
    # Folder with StreamEmber.Scripting.GTAV.dll / StreamEmber.Scripting.RDR2.dll (trainer compile references)
    [string]$ScriptingDir = '',
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
$Platform = Split-Path $Root -Parent   # StreamEmberPlatform\ (sibling repositories)
Import-Module (Join-Path $Root 'tools\StreamEmber.Build.psm1') -Force

$ThirdParty = Join-Path $Root 'third_party'
$CefDir = Join-Path $ThirdParty 'cef'
$CefMarker = Join-Path $CefDir '.streamember-cef-version'
$CefLock = Join-Path $Root 'cef.lock'
$MHudLock = Join-Path $Root 'mhud.lock'
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
        RuntimeRepo = 'gtav-runtime-scripthook'
        Project = 'trainers\gtav\StreamEmber.Trainer.GTAV.csproj'
        # Files of the old layout (OverlayRuntime builds before the StreamEmber layout)
        Conflicts = @('scripts/StreamEmber.Overlay.Bridge.dll', 'scripts/StreamEmber.TrainerDemo.dll',
                      'scripts/OverlayDemo.3.cs', 'StreamEmber/Overlay/overlay.ini')
        Requires = @([ordered]@{ file = 'ScriptHookV.dll'; name = 'Script Hook V (Alexander Blade)'; url = 'http://www.dev-c.com/gtav/scripthookv/' })
    }
    RDR2 = @{
        Name = 'StreamEmber Overlay (RDR2)'; Exe = 'RDR2.exe'; Process = 'RDR2'; Env = 'RDR2_GAME_PATH'
        RuntimeRepo = 'rdr2-runtime-scripthook'
        Project = 'trainers\rdr2\StreamEmber.Trainer.RDR2.csproj'
        Conflicts = @('scripts/StreamEmber.Overlay.Bridge.dll', 'scripts/StreamEmber.Rdr2TrainerDemo.dll',
                      'StreamEmber/Overlay/overlay.ini')
        Requires = @([ordered]@{ file = 'ScriptHookRDR2.dll'; name = 'Script Hook RDR2 (Alexander Blade)'; url = 'http://www.dev-c.com/rdr2/scripthookrdr2/' },
                     [ordered]@{ file = 'dinput8.dll'; name = 'ASI Loader (Script Hook RDR2 package)'; url = 'http://www.dev-c.com/rdr2/scripthookrdr2/' })
    }
}
$Preserve = @('StreamEmber/Config/Overlay.ini')
$Selected = if ($Game -eq 'All') { @($Games.Keys) } else { @($Game) }
if ($Deploy -and $Game -eq 'All') { throw '-Deploy needs a single game: -Game GTAV or -Game RDR2.' }

function Write-Title([string]$Text) { Write-Host ''; Write-Host $Text -ForegroundColor Cyan }
function Write-Ok([string]$Text) { Write-Host "  [OK] $Text" -ForegroundColor Green }

function Get-GitCommit([string]$Path) {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) { return $null }
    $out = & $git.Source -C $Path rev-parse HEAD 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ($out | Out-String).Trim()
}

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

# --- MHud -------------------------------------------------------------------------------------------------------
function Resolve-MHud {
    Write-Title 'MHud'
    $lock = Get-Content $MHudLock -Raw | ConvertFrom-Json
    $path = if ($MHudPath) { $MHudPath } elseif ($env:MHUD_PATH) { $env:MHUD_PATH } else { Join-Path $Platform 'mhud' }
    if (-not (Test-Path (Join-Path $path 'integration\mhud\html\index.html'))) {
        throw "MHud not found in $path. Clone https://github.com/$($lock.repository) next to ui-runtime or pass -MHudPath."
    }
    $path = (Resolve-Path $path).Path
    $commit = Get-GitCommit $path
    if ($commit -and $commit -ne $lock.commit) {
        Write-Warning "MHud in $path is at $commit; mhud.lock pins $($lock.ref) ($($lock.commit)). CI builds the pinned commit."
    }
    $version = (Get-Content (Join-Path $path 'package.json') -Raw | ConvertFrom-Json).version
    Write-Ok "MHud $version ($path)"
    return [pscustomobject]@{ Path = $path; Version = $version; Commit = $commit }
}

# MHud's FiveM page (integration/mhud/html) + kit, unchanged; trainer.html = that page + trainer.css/trainer.js
function Copy-MHudUi($MHud, [string]$UiDir) {
    $page = Join-Path $MHud.Path 'integration\mhud\html'
    $target = Join-Path $UiDir 'mhud'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item (Join-Path $MHud.Path 'kit') $target -Recurse -Force
    foreach ($f in 'index.html', 'app.js', 'app.css') { Copy-Item (Join-Path $page $f) $target -Force }

    $html = [IO.File]::ReadAllText((Join-Path $page 'index.html'), [Text.Encoding]::UTF8)
    $headAnchor = '</head>'
    $appAnchor = '<script src="app.js"></script>'
    if (-not $html.Contains($headAnchor) -or -not $html.Contains($appAnchor)) {
        throw "MHud index.html changed ('$headAnchor' / '$appAnchor' missing); cannot generate trainer.html."
    }
    $html = $html.Replace($headAnchor, "<link rel=`"stylesheet`" href=`"../trainer/trainer.css`">`n$headAnchor")
    $html = $html.Replace($appAnchor, "<script src=`"../trainer/trainer.js`"></script>`n$appAnchor")
    [IO.File]::WriteAllText((Join-Path $target 'trainer.html'), $html, (New-Object Text.UTF8Encoding($false)))
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
function Find-Scripting([string]$Name) {
    $file = "StreamEmber.Scripting.$Name.dll"
    $candidates = @()
    if ($ScriptingDir) { $candidates += Join-Path $ScriptingDir $file }
    $candidates += Join-Path $Platform "$($Games[$Name].RuntimeRepo)\bin\Release\$file"
    foreach ($c in $candidates) { if (Test-Path $c) { return (Resolve-Path $c).Path } }
    throw "$file not found (looked in: $($candidates -join '; ')). Build $($Games[$Name].RuntimeRepo) or pass -ScriptingDir."
}

# Builds bridge + trainer; returns the output folder and the runtime version the trainer was compiled against
function Build-Managed([string]$Name) {
    $dotnet = Get-Command dotnet -ErrorAction SilentlyContinue
    if (-not $dotnet) { throw '.NET SDK not found (dotnet).' }
    $scripting = Find-Scripting $Name
    $runtimeVersion = (Get-Item $scripting).VersionInfo.ProductVersion
    $out = Join-Path $BuildDir "managed\$Name"
    if (Test-Path $out) { Remove-Item $out -Recurse -Force }
    & $dotnet.Source build (Join-Path $Root $Games[$Name].Project) -c $Configuration -o $out --nologo -v minimal `
        "-p:SE_VERSION=$Version" "-p:ScriptingReference=$scripting" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "$Name trainer build failed ($LASTEXITCODE)." }
    Write-Ok "$Name trainer (against $(Split-Path $scripting -Leaf) $runtimeVersion)"
    return [pscustomobject]@{ Dir = $out; RuntimeVersion = $runtimeVersion }
}

# --- Package ----------------------------------------------------------------------------------------------------
function New-GamePackage([string]$Name, $Managed, $MHud) {
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
    Copy-Item (Join-Path $Root 'ui') $overlayDir -Recurse
    Copy-MHudUi $MHud (Join-Path $overlayDir 'ui')

    Copy-Item (Join-Path $Managed.Dir 'StreamEmber.Overlay.Bridge.dll') $scriptsDir
    Copy-Item (Join-Path $Managed.Dir "StreamEmber.Trainer.$Name.dll") $scriptsDir
    Copy-Item (Join-Path $Root 'package\Config\Overlay.ini') $configDir

    Copy-Item (Join-Path $Root 'THIRD-PARTY-NOTICES.md') (Join-Path $licenseDir 'THIRD-PARTY-NOTICES.txt')
    Copy-Item (Join-Path $CefDir 'LICENSE.txt') (Join-Path $licenseDir 'CEF.LICENSE.txt')
    Copy-Item (Join-Path $MHud.Path 'LICENSE') (Join-Path $licenseDir 'MHud.LICENSE.txt')
    if ($Name -eq 'RDR2') {
        $minhook = Get-ChildItem (Join-Path $BuildDir '_deps') -Recurse -File -Filter 'LICENSE.txt' -ErrorAction SilentlyContinue |
            Where-Object { $_.Directory.Name -eq 'minhook-src' } | Select-Object -First 1
        if (-not $minhook) { throw 'MinHook LICENSE.txt not found under build\_deps\minhook-src.' }
        Copy-Item $minhook.FullName (Join-Path $licenseDir 'MinHook.LICENSE.txt')
    }

    $depends = @([ordered]@{ id = "StreamEmber.Runtime.$Name"; builtAgainst = $Managed.RuntimeVersion })
    New-SEManifest -StageDirectory $stage -Id $id -Name $info.Name -Version $Version -Game $Name `
        -Preserve $Preserve -Conflicts $info.Conflicts -Requires $info.Requires -Depends $depends `
        -RepositoryRoot $Root | Out-Null
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
    if (-not (Test-Path (Join-Path $gameDir "StreamEmber.Runtime.$Name.asi"))) {
        Write-Warning "StreamEmber.Runtime.$Name.asi missing: the trainer needs StreamEmber Runtime ($($info.RuntimeRepo))."
    }
    Write-Ok "Installed into $gameDir (logs: StreamEmber\Logs\Overlay*.log)"
}

# --- Main -------------------------------------------------------------------------------------------------------
if (-not $Version) { $Version = Get-SEVersion -RepositoryRoot $Root -Kind Dev }
Write-Host "StreamEmber Overlay $Version ($($Selected -join ', '))" -ForegroundColor Cyan

$cef = Install-Cef
$mhud = Resolve-MHud
Build-Native

Write-Title 'C# (bridge + trainers)'
$managed = @{}
foreach ($name in $Selected) { $managed[$name] = Build-Managed $name }

Write-Title 'Packages'
$stages = @{}
foreach ($name in $Selected) { $stages[$name] = New-GamePackage $name $managed[$name] $mhud }

if ($Deploy) { Install-ToGame $Game $stages[$Game] }
Write-Host ''
Write-Ok "StreamEmber Overlay $Version (CEF $($cef.version), MHud $($mhud.Version), $Configuration)"
