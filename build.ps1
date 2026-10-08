#Requires -Version 5.1
<#
.SYNOPSIS
    StreamEmber OverlayRuntime: CEF'i indirir, C++ projelerini ve C# köprüsünü derler, dist\ altında paketler,
    istenirse GTA V ya da RDR2 klasörüne kurar.

.DESCRIPTION
    1. CEF (minimal dağıtım) third_party\cef altına indirilir. Sürüm cef.lock dosyasına yazılır; sonraki
       derlemeler aynı sürümü kullanır. Yeni sürüme geçmek için: -UpdateCef
    2. CMake ile Visual Studio projesi oluşturulur (build\) ve derlenir.
    3. Her oyun için dist\<oyun>\ klasörü oyun klasörü düzeninde hazırlanır:
         dist\GTAV\StreamEmber.Overlay.GTAV.asi      dist\RDR2\StreamEmber.Overlay.RDR2.asi
         dist\<oyun>\StreamEmber\Overlay\   (çekirdek DLL, host exe, CEF dosyaları, ui\, overlay.ini)
         dist\<oyun>\scripts\               (StreamEmber.Overlay.Bridge.dll + o oyunun trainer demosu)
       ui\mhud\ altına ../MHud kiti ve FiveM sayfası kopyalanır; trainer.html bu sayfaya trainer.js eklenerek üretilir.
    4. -Deploy verilirse -Game ile seçilen oyunun dist\ klasörü oyun klasörüne kopyalanır (overlay.ini korunur).

    Ön koşullar: Visual Studio 2022+ ("Desktop development with C++"), CMake 3.21+ (VS ile gelen de olur),
    .NET SDK (köprü için). İnternet: cef-builds.spotifycdn.com

.PARAMETER Configuration
    Release (varsayılan) ya da Debug.
.PARAMETER Deploy
    dist\ içeriğini oyun klasörüne kopyalar. Oyun kapalı olmalı.
.PARAMETER Game
    Kurulacak oyun: GTAV (varsayılan) ya da RDR2. Derleme her zaman ikisini de üretir.
.PARAMETER GamePath
    Oyun klasörü (GTA5.exe / RDR2.exe'nin olduğu yer). Verilmezse GTA V için GTAV_GAME_PATH (yoksa
    GTAV_SCRIPT_PATH'in üst klasörü), RDR2 için RDR2_GAME_PATH.
.PARAMETER UpdateCef
    cef.lock'u yok sayar, desteklenen aralıktaki en yeni stable CEF'e geçer.
.PARAMETER SkipBridge
    C# köprüsünü ve trainer demosunu derlemez.
.PARAMETER ResetConfig
    Kurulumda oyundaki overlay.ini'yi şablonla değiştirir (varsayılan: korunur).

.EXAMPLE
    .\build.ps1
.EXAMPLE
    .\build.ps1 -Deploy
.EXAMPLE
    .\build.ps1 -Deploy -GamePath "D:\EpicGames\GTAV"
.EXAMPLE
    .\build.ps1 -Deploy -Game RDR2 -GamePath "D:\SteamLibrary\steamapps\common\Red Dead Redemption 2"
#>
[CmdletBinding()]
param(
    [ValidateSet('Release', 'Debug')]
    [string]$Configuration = 'Release',
    [switch]$Deploy,
    [ValidateSet('GTAV', 'RDR2')]
    [string]$Game = 'GTAV',
    [string]$GamePath,
    [switch]$UpdateCef,
    [switch]$SkipBridge,
    [switch]$ResetConfig
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$Root = $PSScriptRoot
$ThirdParty = Join-Path $Root 'third_party'
$CefDir = Join-Path $ThirdParty 'cef'
$CefMarker = Join-Path $CefDir '.streamember-cef-version'
$LockFile = Join-Path $Root 'cef.lock'
$BuildDir = Join-Path $Root 'build'
$DistDir = Join-Path $Root 'dist'
$MHudDir = Join-Path (Split-Path $Root -Parent) 'MHud'
$CefIndexUrl = 'https://cef-builds.spotifycdn.com/index.json'
$CefCdn = 'https://cef-builds.spotifycdn.com/'
# Kodun derlenerek kontrol edildiği CEF ana sürüm aralığı (Chromium ana sürümüyle aynı)
$CefMajorMin = 152
$CefMajorMax = 156

function Write-Title([string]$Text) { Write-Host ''; Write-Host $Text -ForegroundColor Cyan }
function Write-Ok([string]$Text) { Write-Host "  [OK] $Text" -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host "  [!]  $Text" -ForegroundColor Yellow }

function Get-CefVersionKey([string]$CefVersion) {
    # "154.0.7+gabc1234+chromium-154.0.8037.98" -> [version]154.0.7
    $core = $CefVersion.Split('+')[0]
    try { return [version]$core } catch { return [version]'0.0' }
}

function Resolve-CefBuild {
    Write-Host "  CEF listesi okunuyor: $CefIndexUrl"
    $index = Invoke-RestMethod -Uri $CefIndexUrl -UseBasicParsing
    $candidates = @($index.windows64.versions | Where-Object {
        $major = (Get-CefVersionKey $_.cef_version).Major
        $_.channel -eq 'stable' -and $major -ge $CefMajorMin -and $major -le $CefMajorMax
    } | Sort-Object -Property @{ Expression = { Get-CefVersionKey $_.cef_version } } -Descending)
    if ($candidates.Count -eq 0) {
        $available = ($index.windows64.versions | Where-Object { $_.channel -eq 'stable' } |
            Select-Object -First 5 | ForEach-Object { $_.cef_version }) -join ', '
        throw "Desteklenen aralıkta ($CefMajorMin-$CefMajorMax) stable CEF yok. Mevcut stable: $available"
    }
    $pick = $candidates[0]
    $file = @($pick.files | Where-Object { $_.type -eq 'minimal' })[0]
    if (-not $file) { throw "CEF $($pick.cef_version) için minimal dağıtım bulunamadı." }
    return [pscustomobject]@{ version = $pick.cef_version; chromium = $pick.chromium_version; name = $file.name; sha1 = $file.sha1 }
}

function Expand-TarBz2([string]$Archive, [string]$Destination) {
    $tar = Get-Command tar.exe -ErrorAction SilentlyContinue
    if ($tar) {
        & $tar.Source -xjf $Archive -C $Destination | Out-Host
        if ($LASTEXITCODE -eq 0) { return }
        Write-Warn "tar.exe arşivi açamadı, 7-Zip deneniyor."
    }
    $sevenZip = Get-Command 7z.exe -ErrorAction SilentlyContinue
    if (-not $sevenZip) {
        $candidate = Join-Path $env:ProgramFiles '7-Zip\7z.exe'
        if (Test-Path $candidate) { $sevenZip = Get-Item $candidate }
    }
    if (-not $sevenZip) { throw 'tar.bz2 açılamadı: Windows tar.exe ya da 7-Zip gerekli.' }
    $path7z = if ($sevenZip.Source) { $sevenZip.Source } else { $sevenZip.FullName }
    & $path7z x $Archive "-o$Destination" -y | Out-Null
    $inner = Join-Path $Destination ([IO.Path]::GetFileNameWithoutExtension($Archive))
    & $path7z x $inner "-o$Destination" -y | Out-Null
    Remove-Item $inner -Force
}

function Install-Cef {
    Write-Title '1/4  CEF'
    $lock = $null
    if ((Test-Path $LockFile) -and -not $UpdateCef) {
        $lock = Get-Content $LockFile -Raw | ConvertFrom-Json
    } else {
        $lock = Resolve-CefBuild
        $lock | ConvertTo-Json | Set-Content -Path $LockFile -Encoding UTF8
        Write-Ok "cef.lock yazıldı: $($lock.version)"
    }

    if ((Test-Path $CefMarker) -and ((Get-Content $CefMarker -Raw).Trim() -eq $lock.version)) {
        Write-Ok "CEF hazır: $($lock.version)"
        return $lock
    }

    New-Item -ItemType Directory -Force -Path $ThirdParty | Out-Null
    $archive = Join-Path $ThirdParty $lock.name
    if (-not (Test-Path $archive)) {
        Write-Host "  İndiriliyor: $($lock.name)"
        $curl = Get-Command curl.exe -ErrorAction SilentlyContinue
        if ($curl) {
            & $curl.Source -L --fail -o $archive ($CefCdn + $lock.name) | Out-Host
            if ($LASTEXITCODE -ne 0) { throw "CEF indirilemedi (curl $LASTEXITCODE)." }
        } else {
            Invoke-WebRequest -Uri ($CefCdn + $lock.name) -OutFile $archive -UseBasicParsing
        }
    }
    $hash = (Get-FileHash -Path $archive -Algorithm SHA1).Hash.ToLowerInvariant()
    if ($hash -ne $lock.sha1.ToLowerInvariant()) {
        Remove-Item $archive -Force
        throw "CEF arşivinin SHA1 değeri tutmuyor (beklenen $($lock.sha1), gelen $hash). Tekrar deneyin."
    }

    Write-Host '  Açılıyor...'
    if (Test-Path $CefDir) { Remove-Item $CefDir -Recurse -Force }
    Expand-TarBz2 $archive $ThirdParty
    $extracted = Join-Path $ThirdParty ($lock.name -replace '\.tar\.bz2$', '')
    if (-not (Test-Path $extracted)) { throw "Beklenen klasör yok: $extracted" }
    Rename-Item $extracted $CefDir
    Set-Content -Path $CefMarker -Value $lock.version -Encoding ASCII
    Remove-Item $archive -Force
    Write-Ok "CEF kuruldu: $($lock.version) (Chromium $($lock.chromium))"
    return $lock
}

function Find-CMake {
    $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vs = & $vswhere -latest -prerelease -products * -property installationPath
        $candidate = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        if (Test-Path $candidate) { return $candidate }
    }
    throw 'cmake.exe bulunamadı. Visual Studio "C++ CMake tools" bileşenini ya da CMake 3.21+ kurun.'
}

function Find-Output([string]$Name) {
    $file = Get-ChildItem -Path $BuildDir -Recurse -File -Filter $Name |
        Where-Object { $_.DirectoryName -match "\\$Configuration$" } | Select-Object -First 1
    if (-not $file) { throw "Derleme çıktısı bulunamadı: $Name" }
    return $file.FullName
}

function Build-Native {
    Write-Title '2/4  C++ (CMake + Visual Studio)'
    $cmake = Find-CMake
    & $cmake -S $Root -B $BuildDir -A x64 "-DCEF_ROOT=$CefDir" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "CMake yapılandırması başarısız ($LASTEXITCODE)." }
    & $cmake --build $BuildDir --config $Configuration --parallel | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Derleme başarısız ($LASTEXITCODE)." }
    Write-Ok 'C++ projeleri derlendi.'
}

# Returns @{ GTAV = <output dir or $null>; RDR2 = <output dir or $null> }
function Build-Managed {
    Write-Title '3/4  C# köprüsü + trainer demoları'
    $result = @{ GTAV = $null; RDR2 = $null }
    if ($SkipBridge) { Write-Warn 'Atlandı (-SkipBridge).'; return $result }
    $dotnet = Get-Command dotnet.exe -ErrorAction SilentlyContinue
    if (-not $dotnet) { Write-Warn '.NET SDK yok; C# projeleri atlandı.'; return $result }

    # Each trainer references the bridge project, so one build produces both DLLs
    $out = Join-Path $BuildDir 'managed\gtav'
    $project = Join-Path $Root 'samples\gtav\TrainerDemo\StreamEmber.TrainerDemo.csproj'
    & $dotnet.Source build $project -c $Configuration -o $out --nologo -v minimal | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "GTA V trainer derlenemedi ($LASTEXITCODE). SHVDN başvurusu: ..\GTAVScriptHookRuntime\bin\Release\ScriptHookVDotNet3.dll" }
    Write-Ok 'GTA V: StreamEmber.Overlay.Bridge.dll + StreamEmber.TrainerDemo.dll'
    $result.GTAV = $out

    $rdrApi = Join-Path (Split-Path $Root -Parent) 'RDR2ScriptHookRuntime\bin\Release\ScriptHookRDRNetAPI.dll'
    if (Test-Path $rdrApi) {
        $out = Join-Path $BuildDir 'managed\rdr2'
        $project = Join-Path $Root 'samples\rdr2\TrainerDemo\StreamEmber.Rdr2TrainerDemo.csproj'
        & $dotnet.Source build $project -c $Configuration -o $out --nologo -v minimal | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "RDR2 trainer derlenemedi ($LASTEXITCODE)." }
        Write-Ok 'RDR2: StreamEmber.Overlay.Bridge.dll + StreamEmber.Rdr2TrainerDemo.dll'
        $result.RDR2 = $out
    } else {
        Write-Warn "RDR2 trainer atlandı: önce ..\RDR2ScriptHookRuntime\build.ps1 çalıştırın ($rdrApi yok)."
    }
    return $result
}

# MHud's FiveM page (integration/mhud/html) + kit, unchanged; trainer.html = that page + trainer.css/trainer.js
function Copy-MHudUi([string]$UiDir) {
    $kit = Join-Path $MHudDir 'kit'
    $page = Join-Path $MHudDir 'integration\mhud\html'
    if (-not (Test-Path (Join-Path $page 'index.html')) -or -not (Test-Path $kit)) {
        Write-Warn "MHud bulunamadı ($MHudDir); trainer arayüzü atlandı."
        return
    }
    $target = Join-Path $UiDir 'mhud'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    Copy-Item $kit $target -Recurse -Force
    foreach ($f in 'index.html', 'app.js', 'app.css') { Copy-Item (Join-Path $page $f) $target -Force }

    $html = [IO.File]::ReadAllText((Join-Path $page 'index.html'), [Text.Encoding]::UTF8)
    $headAnchor = '</head>'
    $appAnchor = '<script src="app.js"></script>'
    if (-not $html.Contains($headAnchor) -or -not $html.Contains($appAnchor)) {
        throw "MHud index.html beklenen yapıda değil ('$headAnchor' / '$appAnchor' yok); trainer.html üretilemedi."
    }
    $html = $html.Replace($headAnchor, "<link rel=`"stylesheet`" href=`"../trainer/trainer.css`">`n$headAnchor")
    $html = $html.Replace($appAnchor, "<script src=`"../trainer/trainer.js`"></script>`n$appAnchor")
    [IO.File]::WriteAllText((Join-Path $target 'trainer.html'), $html, (New-Object Text.UTF8Encoding($false)))
    Write-Ok 'MHud arayüzü + trainer.html hazır.'
}

# Game specific parts of the layout
$Games = @{
    GTAV = @{ Asi = 'StreamEmber.Overlay.GTAV.asi'; Exe = 'GTA5.exe'; Process = 'GTA5'; Env = 'GTAV_GAME_PATH';
              Trainer = @('StreamEmber.TrainerDemo.dll', 'StreamEmber.TrainerDemo.pdb'); Log = 'gtav-backend.log' }
    RDR2 = @{ Asi = 'StreamEmber.Overlay.RDR2.asi'; Exe = 'RDR2.exe'; Process = 'RDR2'; Env = 'RDR2_GAME_PATH';
              Trainer = @('StreamEmber.Rdr2TrainerDemo.dll', 'StreamEmber.Rdr2TrainerDemo.pdb'); Log = 'rdr2-backend.log' }
}

function New-GameDist([string]$Name, [string]$ManagedDir) {
    $info = $Games[$Name]
    $dist = Join-Path $DistDir $Name
    $overlayDir = Join-Path $dist 'StreamEmber\Overlay'
    $scriptsDir = Join-Path $dist 'scripts'
    New-Item -ItemType Directory -Force -Path $overlayDir, $scriptsDir | Out-Null

    Copy-Item (Find-Output $info.Asi) $dist
    Copy-Item (Find-Output 'StreamEmber.Overlay.dll') $overlayDir
    Copy-Item (Find-Output 'StreamEmber.Overlay.Host.exe') $overlayDir

    # CEF çalışma dosyaları: Release\ (dll, bin, json) + Resources\ (pak, icudtl.dat, locales\)
    $cefBin = Join-Path $CefDir 'Release'
    Get-ChildItem $cefBin -File | Where-Object { $_.Extension -in '.dll', '.bin', '.json' } |
        ForEach-Object { Copy-Item $_.FullName $overlayDir }
    Copy-Item (Join-Path $CefDir 'Resources\*') $overlayDir -Recurse -Force

    Copy-Item (Join-Path $Root 'ui') $overlayDir -Recurse
    Copy-MHudUi (Join-Path $overlayDir 'ui')
    Copy-Item (Join-Path $Root 'overlay.ini') $overlayDir

    # Only one script may consume UI messages, so the old OverlayDemo.3.cs sample is not deployed with the trainer.
    if ($ManagedDir -and (Test-Path $ManagedDir)) {
        foreach ($f in @('StreamEmber.Overlay.Bridge.dll') + $info.Trainer) {
            $p = Join-Path $ManagedDir $f
            if (Test-Path $p) { Copy-Item $p $scriptsDir }
        }
    }
    $size = (Get-ChildItem $dist -Recurse -File | Measure-Object -Property Length -Sum).Sum / 1MB
    Write-Ok ("dist\{0}\ hazır ({1:N0} MB)" -f $Name, $size)
}

function New-Dist($Managed) {
    Write-Title '4/4  dist\'
    if (Test-Path $DistDir) { Remove-Item $DistDir -Recurse -Force }
    New-GameDist 'GTAV' $Managed.GTAV
    New-GameDist 'RDR2' $Managed.RDR2
}

function Resolve-GamePath {
    if ($GamePath) { return $GamePath }
    $fromEnv = [Environment]::GetEnvironmentVariable($Games[$Game].Env)
    if ($fromEnv) { return $fromEnv }
    if ($Game -eq 'GTAV' -and $env:GTAV_SCRIPT_PATH) { return (Split-Path $env:GTAV_SCRIPT_PATH -Parent) }
    throw "Oyun klasörü bilinmiyor: -GamePath verin ya da $($Games[$Game].Env) ortam değişkenini ayarlayın."
}

function Install-ToGame {
    Write-Title "Kurulum ($Game)"
    $info = $Games[$Game]
    $gameDir = Resolve-GamePath  # not $game: PowerShell names are case-insensitive ($Game = parameter)
    if (-not (Test-Path (Join-Path $gameDir $info.Exe))) { throw "$($info.Exe) bulunamadı: $gameDir" }
    if (Get-Process -Name $info.Process -ErrorAction SilentlyContinue) { throw "$Game açık; dosyalar kilitli. Oyunu kapatın." }
    $dist = Join-Path $DistDir $Game

    Copy-Item (Join-Path $dist $info.Asi) $gameDir -Force
    $target = Join-Path $gameDir 'StreamEmber\Overlay'
    New-Item -ItemType Directory -Force -Path $target | Out-Null
    $source = Join-Path $dist 'StreamEmber\Overlay'
    Get-ChildItem $source | ForEach-Object {
        if ($_.Name -eq 'overlay.ini' -and (Test-Path (Join-Path $target 'overlay.ini')) -and -not $ResetConfig) {
            Write-Warn 'overlay.ini zaten var; korunuyor (şablonu yazmak için -ResetConfig). Trainer için: StartUrl=mhud/trainer.html'
        } else {
            Copy-Item $_.FullName $target -Recurse -Force
        }
    }
    $scripts = Join-Path $gameDir 'scripts'
    New-Item -ItemType Directory -Force -Path $scripts | Out-Null
    $oldDemo = Join-Path $scripts 'OverlayDemo.3.cs'
    if (Test-Path $oldDemo) {
        Move-Item $oldDemo "$oldDemo.disabled" -Force
        Write-Warn 'scripts\OverlayDemo.3.cs devre dışı bırakıldı (.disabled): trainer ile aynı mesaj kuyruğunu okuyordu.'
    }
    if (Test-Path (Join-Path $dist 'scripts\*')) { Copy-Item (Join-Path $dist 'scripts\*') $scripts -Force }
    if ($Game -eq 'RDR2') {
        foreach ($need in 'ScriptHookRDR2.dll', 'dinput8.dll', 'ScriptHookRDRDotNet.asi') {
            if (-not (Test-Path (Join-Path $gameDir $need))) { Write-Warn "$need oyun klasöründe yok (ScriptHookRDR2 / RDR2ScriptHookRuntime kurulu mu?)." }
        }
    }
    Write-Ok "Kuruldu: $gameDir"
    Write-Host "  Loglar: StreamEmber\Overlay\logs\ ($($info.Log), overlay.log, cef.log)"
}

$lock = Install-Cef
Build-Native
$managed = Build-Managed
New-Dist $managed
if ($Deploy) { Install-ToGame }
Write-Host ''
Write-Ok "Bitti. CEF $($lock.version), $Configuration."
