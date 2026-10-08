# StreamEmber.Build.psm1 — shared build helpers of the StreamEmber Platform repositories.
# The same file lives in gtav-runtime-scripthook, rdr2-runtime-scripthook and ui-runtime (tools/); keep them identical.
#
#   Get-SEVersion        product version from the VERSION file (major.minor) + git history (patch)
#   Find-SEMSBuild       MSBuild.exe of the newest Visual Studio (vswhere)
#   New-SEManifest       StreamEmber\Manifests\<id>.json with file hashes for installers
#   New-SEPackage        zip + .sha256 of a staged game-folder layout
#   Install-SEPackage    copy a staged layout into a game folder (keeps user config, disables conflicting files)

Set-StrictMode -Version 3.0

function Invoke-SEGit {
    param([Parameter(Mandatory)][string[]]$Arguments, [string]$WorkingDirectory = (Get-Location).Path)
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) { return $null }
    $output = & $git.Source -C $WorkingDirectory @Arguments 2>$null
    if ($LASTEXITCODE -ne 0) { return $null }
    return ($output | Out-String).Trim()
}

<#
.SYNOPSIS
    Product version: <major>.<minor> from VERSION, patch = commits since VERSION last changed.
.DESCRIPTION
    Every commit on main is a new patch number, so every push can be released automatically. Raising the minor or
    major number = editing VERSION (the patch starts again at 0 with that commit).
    Kind:
      Release  1.0.17            (CI on main)
      CI       1.0.17-ci.<run>   (CI on other branches / pull requests)
      Dev      1.0.17-dev        (local builds; never confused with a released file)
#>
function Get-SEVersion {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [ValidateSet('Release', 'CI', 'Dev')][string]$Kind = 'Dev',
        [string]$RunNumber = ''
    )
    $versionFile = Join-Path $RepositoryRoot 'VERSION'
    if (-not (Test-Path $versionFile)) { throw "VERSION file missing: $versionFile" }
    $base = (Get-Content $versionFile -Raw).Trim()
    if ($base -notmatch '^\d+\.\d+$') { throw "VERSION must be <major>.<minor> (found '$base')." }

    $patch = 0
    $since = Invoke-SEGit -WorkingDirectory $RepositoryRoot -Arguments @('log', '-1', '--format=%H', '--', 'VERSION')
    if ($since) {
        $count = Invoke-SEGit -WorkingDirectory $RepositoryRoot -Arguments @('rev-list', '--count', "$since..HEAD")
        if ($count -match '^\d+$') { $patch = [int]$count }
    } elseif ($Kind -eq 'Release') {
        throw 'Release versions need the git history (actions/checkout with fetch-depth: 0).'
    }

    $version = "$base.$patch"
    switch ($Kind) {
        'Release' { return $version }
        'CI'      { return "$version-ci.$RunNumber" }
        default   { return "$version-dev" }
    }
}

function Find-SEMSBuild {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found. Install Visual Studio 2022 or later.' }
    $msbuild = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild `
        -find 'MSBuild\**\Bin\amd64\MSBuild.exe' | Select-Object -First 1
    if (-not $msbuild) { throw 'MSBuild.exe not found.' }
    return $msbuild
}

<#
.SYNOPSIS
    Writes StreamEmber\Manifests\<Id>.json into a staged layout (paths relative to the game folder).
#>
function New-SEManifest {
    param(
        [Parameter(Mandatory)][string]$StageDirectory,
        [Parameter(Mandatory)][string]$Id,
        [Parameter(Mandatory)][string]$Name,
        [Parameter(Mandatory)][string]$Version,
        [Parameter(Mandatory)][ValidateSet('GTAV', 'RDR2')][string]$Game,
        [string[]]$Preserve = @(),
        [string[]]$Conflicts = @(),
        [object[]]$Requires = @(),
        [object[]]$Depends = @(),
        [string]$RepositoryRoot = ''
    )
    $executables = @{ GTAV = 'GTA5.exe'; RDR2 = 'RDR2.exe' }
    $manifestDir = Join-Path $StageDirectory 'StreamEmber\Manifests'
    New-Item -ItemType Directory -Force -Path $manifestDir | Out-Null
    $manifestPath = Join-Path $manifestDir "$Id.json"
    if (Test-Path $manifestPath) { Remove-Item $manifestPath -Force }

    $root = (Resolve-Path $StageDirectory).Path.TrimEnd('\', '/')
    $files = @(Get-ChildItem $StageDirectory -Recurse -File | Sort-Object FullName | ForEach-Object {
        [ordered]@{
            path   = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
            size   = $_.Length
            sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    })
    $commit = if ($RepositoryRoot) { Invoke-SEGit -WorkingDirectory $RepositoryRoot -Arguments @('rev-parse', 'HEAD') } else { $null }

    $manifest = [ordered]@{
        schema         = 1
        id             = $Id
        name           = $Name
        version        = $Version
        game           = $Game
        gameExecutable = $executables[$Game]
        commit         = $commit
        builtUtc       = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        requires       = @($Requires)
        depends        = @($Depends)
        preserve       = @($Preserve)
        conflicts      = @($Conflicts)
        files          = $files
    }
    $json = $manifest | ConvertTo-Json -Depth 6
    [IO.File]::WriteAllText($manifestPath, $json + "`n", (New-Object Text.UTF8Encoding($false)))
    return $manifestPath
}

<#
.SYNOPSIS
    Zips a staged layout to <OutputDirectory>\<Id>-<Version>.zip and writes <zip>.sha256.
#>
function New-SEPackage {
    param(
        [Parameter(Mandatory)][string]$StageDirectory,
        [Parameter(Mandatory)][string]$OutputDirectory,
        [Parameter(Mandatory)][string]$Id,
        [Parameter(Mandatory)][string]$Version
    )
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $zip = Join-Path $OutputDirectory "$Id-$Version.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path (Join-Path $StageDirectory '*') -DestinationPath $zip -CompressionLevel Optimal
    $hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText("$zip.sha256", "$hash  $(Split-Path $zip -Leaf)`n", (New-Object Text.UTF8Encoding($false)))
    return $zip
}

<#
.SYNOPSIS
    Copies a staged layout into the game folder.
.DESCRIPTION
    - Files listed in -Preserve (user settings) are only written when missing, unless -ResetConfig.
    - Files listed in -Conflicts that exist in the game folder are renamed to <name>.disabled (never deleted).
#>
function Install-SEPackage {
    param(
        [Parameter(Mandatory)][string]$StageDirectory,
        [Parameter(Mandatory)][string]$GameDirectory,
        [Parameter(Mandatory)][string]$GameExecutable,
        [string]$ProcessName = '',
        [string[]]$Preserve = @(),
        [string[]]$Conflicts = @(),
        [switch]$ResetConfig
    )
    if (-not (Test-Path (Join-Path $GameDirectory $GameExecutable))) {
        throw "$GameExecutable not found in $GameDirectory"
    }
    if ($ProcessName -and (Get-Process -Name $ProcessName -ErrorAction SilentlyContinue)) {
        throw "$ProcessName is running; its files are locked. Close the game."
    }
    foreach ($conflict in $Conflicts) {
        $path = Join-Path $GameDirectory $conflict
        if (Test-Path $path) {
            $disabled = "$path.disabled"
            if (Test-Path $disabled) { Remove-Item $disabled -Force }
            Move-Item $path $disabled
            Write-Warning "Disabled conflicting file: $conflict -> $conflict.disabled"
        }
    }
    $root = (Resolve-Path $StageDirectory).Path.TrimEnd('\', '/')
    $preserved = @($Preserve | ForEach-Object { $_.Replace('\', '/') })
    foreach ($file in Get-ChildItem $StageDirectory -Recurse -File) {
        $relative = $file.FullName.Substring($root.Length + 1)
        $target = Join-Path $GameDirectory $relative
        if ($preserved -contains $relative.Replace('\', '/') -and (Test-Path $target) -and -not $ResetConfig) {
            Write-Host "  kept (user settings): $relative"
            continue
        }
        New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
        Copy-Item $file.FullName $target -Force
    }
}

Export-ModuleMember -Function Get-SEVersion, Find-SEMSBuild, New-SEManifest, New-SEPackage, Install-SEPackage
