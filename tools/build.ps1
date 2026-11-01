# SPDX-License-Identifier: MPL-2.0
param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$BuildDirectory='')
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if(-not $BuildDirectory){$BuildDirectory=Join-Path $project 'build'}
$vswhere="${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$installation=& $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $installation){throw 'Install the Visual Studio 2022 C++ desktop build tools first.'}
cmake -S $project -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_GENERATOR_INSTANCE=$installation"
if($LASTEXITCODE){throw 'CMake configure failed'}
cmake --build $BuildDirectory --config $Configuration --parallel
if($LASTEXITCODE){throw 'Build failed'}
ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if($LASTEXITCODE){throw 'Tests failed'}
