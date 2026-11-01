# SPDX-License-Identifier: MPL-2.0
param([string]$ExpectedTag='')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$cmake=Get-Content -LiteralPath "$project\CMakeLists.txt" -Raw
$version=[regex]::Match($cmake,'project\(MythCoolMini\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value
if(-not $version){throw 'Project version was not found'}
if($ExpectedTag -and $ExpectedTag -ne "v$version"){throw "Tag $ExpectedTag does not match v$version"}
& "$PSScriptRoot\bootstrap.ps1"
$build=Join-Path $project 'build\release-build'
& "$PSScriptRoot\build.ps1" -BuildDirectory $build
$exe=Join-Path $build 'Release\MythCool-mini.exe'
$fileVersion=(Get-Item -LiteralPath $exe).VersionInfo.FileVersion
if($fileVersion -ne $version){throw "EXE version $fileVersion does not match $version"}
$stageRoot=[IO.Path]::GetFullPath((Join-Path $project 'build\release-stage'))
if([IO.Path]::GetDirectoryName($stageRoot) -ne [IO.Path]::GetFullPath((Join-Path $project 'build'))){throw 'Unexpected staging directory'}
if(Test-Path -LiteralPath $stageRoot){Remove-Item -LiteralPath $stageRoot -Recurse -Force}
$portable=Join-Path $stageRoot 'portable\MythCool-mini'
$source=Join-Path $stageRoot 'source\MythCool-mini'
$output=Join-Path $project 'releases'
New-Item -ItemType Directory -Force $portable,$source,$output | Out-Null
foreach($name in 'src','assets','tests','tools','docs','licenses','.github','CMakeLists.txt','dependencies.lock.json','README.md','CONTRIBUTING.md','CHANGELOG.md','LICENSE','THIRD-PARTY-NOTICES.md','.clang-format','.gitignore','.gitattributes','.editorconfig') {
 Copy-Item -LiteralPath (Join-Path $project $name) -Destination $source -Recurse -Force
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$sourceZip=Join-Path $output "MythCool-mini-$version-source.zip"
$portableZip=Join-Path $output "MythCool-mini-$version-windows-x64.zip"
foreach($archive in $sourceZip,$portableZip){if(Test-Path -LiteralPath $archive){Remove-Item -LiteralPath $archive -Force}}
[IO.Compression.ZipFile]::CreateFromDirectory($source,$sourceZip,[IO.Compression.CompressionLevel]::Optimal,$true)
foreach($name in 'MythCool-mini.exe','PawnIOLib.dll'){Copy-Item -LiteralPath "$build\Release\$name" -Destination $portable}
Copy-Item -LiteralPath "$project\third_party\AMDFamily17.bin" -Destination $portable
Copy-Item -LiteralPath "$project\docs\PORTABLE-README.txt" -Destination "$portable\README.txt"
Copy-Item -LiteralPath "$project\LICENSE" -Destination "$portable\LICENSE.txt"
Copy-Item -LiteralPath "$project\THIRD-PARTY-NOTICES.md" -Destination $portable
Copy-Item -LiteralPath "$project\licenses" -Destination $portable -Recurse
Copy-Item -LiteralPath $sourceZip -Destination "$portable\source.zip"
[IO.Compression.ZipFile]::CreateFromDirectory($portable,$portableZip,[IO.Compression.CompressionLevel]::Optimal,$true)
foreach($archive in $sourceZip,$portableZip){$hash=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant();[IO.File]::WriteAllText("$archive.sha256","$hash *$([IO.Path]::GetFileName($archive))`n",[Text.Encoding]::ASCII)}
$notes=Get-Content -LiteralPath "$project\docs\RELEASE-NOTES.md" -Raw -Encoding UTF8
if(-not $notes.Contains("v$version")){throw 'Release notes do not match the version'}
[IO.File]::WriteAllText((Join-Path $output 'release-notes.md'),$notes,[Text.UTF8Encoding]::new($false))
Remove-Item -LiteralPath $stageRoot -Recurse -Force
Write-Host "Release archives and SHA256: $output"
