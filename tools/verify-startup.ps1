# SPDX-License-Identifier: MPL-2.0
# Live registration test; removes its current-user task afterwards. Start with autostart off.
param([string]$Executable=(Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\MythCool-mini.exe'))
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
$out=Join-Path $project 'out'
$log=[Collections.Generic.List[object]]::new()
function Check([bool]$ok,[string]$name) {
 $log.Add([ordered]@{check=$name;passed=$ok})
 if(-not $ok){throw "Failed: $name"}
}
function Run-Mini([string]$exe,[string]$operation,[string]$name) {
 $file=Join-Path $out "$name.json"
 $p=Start-Process -FilePath $exe -ArgumentList ('--autostart {0} --output "{1}"' -f $operation,$file) -WindowStyle Hidden -Wait -PassThru
 $j=Get-Content -LiteralPath $file -Raw -Encoding utf8 | ConvertFrom-Json
 if($p.ExitCode -notin @(0,3)){throw "$operation failed: $($j.error)"}
 return $j
}
$initial=Run-Mini $Executable 'status' 'startup-before'
if($initial.registered){throw 'Existing startup registration kept. Disable it before this live test.'}
$taskPath='\MythCool-mini\'
$taskName=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
$fixture=Join-Path $out '自启 & 路径测试'
New-Item -ItemType Directory -Force $fixture | Out-Null
foreach($name in 'MythCool-mini.exe','PawnIOLib.dll','AMDFamily17.bin') {Copy-Item -LiteralPath (Join-Path (Split-Path -Parent $Executable) $name) -Destination $fixture -Force}
$fixtureExe=Join-Path $fixture 'MythCool-mini.exe'
try {
 $on=Run-Mini $fixtureExe 'on' 'startup-unicode-on'
 Check ($on.enabled -and $on.path_matches -and $on.administrator) 'Register/read back Unicode and ampersand executable path'
 $task=Get-ScheduledTask -TaskPath $taskPath -TaskName $taskName
 Check ($task.Principal.RunLevel -eq 'Highest' -and $task.Principal.LogonType -eq 'Interactive') 'Highest privilege, interactive current-user task'
 Check ($task.Actions.Execute -eq $fixtureExe -and $task.Actions.Arguments -eq '--tray' -and $task.Actions.WorkingDirectory -eq $fixture) 'Native scheduler returns exact target, arguments and working directory'
 Check ($task.Triggers.Count -eq 1 -and $task.Triggers[0].Delay -eq 'PT10S') 'Logon trigger with 10-second driver startup allowance'
 Check ($task.Settings.ExecutionTimeLimit -eq 'PT0S') 'No default task runtime limit'
 Disable-ScheduledTask -TaskPath $taskPath -TaskName $taskName | Out-Null
 $disabled=Run-Mini $fixtureExe 'status' 'startup-disabled'
 Check (-not $disabled.enabled) 'Read externally disabled task state'
 $again=Run-Mini $fixtureExe 'on' 'startup-reenabled'
 Check $again.enabled 'Explicit enable restores task'
 $off=Run-Mini $fixtureExe 'off' 'startup-unicode-off'
 Check (-not $off.registered) 'Disable removes current-user registration'
 $off=Run-Mini $fixtureExe 'off' 'startup-off-twice'
 Check (-not $off.registered) 'Repeated disable succeeds'
 $on=Run-Mini $Executable 'on' 'startup-real-on'
 Check ($on.enabled -and $on.path_matches) 'Register actual program location'
 $oldExe=Join-Path $project 'dist\MythCool-mini\MythCool-mini.exe'
 Start-Process -FilePath $oldExe -ArgumentList '--quit' -WindowStyle Hidden -Wait
 Start-Sleep -Milliseconds 600
 Start-ScheduledTask -TaskPath $taskPath -TaskName $taskName
 Start-Sleep -Seconds 4
 $running=@(Get-CimInstance Win32_Process -Filter "Name='MythCool-mini.exe'" | Where-Object {$_.ExecutablePath -eq $Executable})
 Check ($running.Count -eq 1) 'Scheduled task launches exactly one resident mini process'
 Check ((Get-ScheduledTask -TaskPath $taskPath -TaskName $taskName).State -eq 'Running') 'Task remains running while tray process monitors'
 $p=Start-Process -FilePath $Executable -ArgumentList ('--diagnose --output "{0}\startup-six.json"' -f $out) -WindowStyle Hidden -Wait -PassThru
 $six=Get-Content "$out\startup-six.json" -Raw -Encoding utf8 | ConvertFrom-Json
 Check ($p.ExitCode -eq 0 -and $six.administrator -and @($six.values | Where-Object {$_.value -ne '--'}).Count -eq 6) 'Elevated default startup reads all six sensors'
 $running | Select-Object ProcessId,ExecutablePath,CommandLine | ConvertTo-Json | Set-Content "$out\startup-resident-process.json" -Encoding utf8
} finally {
 $final=Run-Mini $Executable 'off' 'startup-final'
 $log.Add([ordered]@{check='Leave login autostart off after test';passed=(-not $final.registered)})
 $log | ConvertTo-Json -Depth 5 | Set-Content "$out\startup-validation.json" -Encoding utf8
}
Start-Process -FilePath $Executable -ArgumentList '--show' -WindowStyle Normal
