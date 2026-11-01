# SPDX-License-Identifier: MPL-2.0
param([switch]$IncludeDriverInstaller)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$deps=Get-Content -LiteralPath "$project\dependencies.lock.json" -Raw | ConvertFrom-Json
foreach($name in 'leveldb','snappy','PawnIO','PawnIO.Modules') {
    $entry=$deps.$name
    $path=Join-Path "$project\third_party" $name
    if(-not (Test-Path -LiteralPath "$path\.git")) {
        New-Item -ItemType Directory -Force $path | Out-Null
        & git --git-dir="$path\.git" --work-tree="$path" init $path
        if($LASTEXITCODE){throw "git init failed: $name"}
        & git --git-dir="$path\.git" --work-tree="$path" remote add origin $entry.repository
        & git --git-dir="$path\.git" --work-tree="$path" fetch --depth 1 origin $entry.commit
        if($LASTEXITCODE){throw "git fetch failed: $name"}
        & git --git-dir="$path\.git" --work-tree="$path" checkout --detach FETCH_HEAD
        if($LASTEXITCODE){throw "git checkout failed: $name"}
    } else {
        $current=& git --git-dir="$path\.git" --work-tree="$path" rev-parse HEAD
        if($current -ne $entry.commit){throw "$name differs from dependencies.lock.json; keep your changes and resolve it before building."}
    }
}
$downloads=@('json.hpp','json-LICENSE.MIT','AMDFamily17.bin')
if($IncludeDriverInstaller){$downloads+='driver/PawnIO_setup.exe'}
foreach($name in $downloads) {
    $entry=$deps.$name
    $path=Join-Path "$project\third_party" $name
    if(-not (Test-Path -LiteralPath $path)) {
        New-Item -ItemType Directory -Force (Split-Path -Parent $path) | Out-Null
        Invoke-WebRequest -Uri $entry.url -OutFile $path
    }
    $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    if($hash -ne $entry.sha256){throw "Unexpected file checksum: $name"}
}
Write-Host 'Dependencies ready. No software or drivers were installed.'
