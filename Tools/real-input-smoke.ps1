param([string]$Exe, [string]$Script, [string]$Prefix)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Native {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
}
"@
[Native]::SetProcessDPIAware() | Out-Null
$env:PATH = "C:\Qt\6.9.0\msvc2022_64\bin;" + $env:PATH
$env:FG_STUDIO_PROBE = $Prefix
Remove-Item "$Prefix.coords","$Prefix.final" -ErrorAction SilentlyContinue
$p = Start-Process -FilePath $Exe -ArgumentList $Script -PassThru
$deadline = (Get-Date).AddSeconds(8)
while (-not (Test-Path "$Prefix.coords") -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
Start-Sleep -Milliseconds 300
Write-Output "--- coords"
Get-Content "$Prefix.coords"
$dpr = 1.0
Get-Content "$Prefix.coords" | ForEach-Object { if ($_ -match 'dpr ([0-9.]+)') { $dpr = [double]$Matches[1] } }
$c = @{}
Get-Content "$Prefix.coords" | ForEach-Object { $t = $_ -split ' '; if ($t[0] -in 'header','from','to') { $c[$t[0]] = @([int]([double]$t[1] * $dpr), [int]([double]$t[2] * $dpr)) } }
Write-Output "dpr=$dpr"

function Drag($from, $to) {
  [Native]::SetCursorPos($from[0], $from[1]) | Out-Null; Start-Sleep -Milliseconds 150
  [Native]::SetCursorPos($from[0]+1, $from[1]) | Out-Null; Start-Sleep -Milliseconds 100
  [Native]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 150     # left down
  for ($i = 1; $i -le 12; $i++) {
    $x = [int]($from[0] + ($to[0]-$from[0]) * $i / 12); $y = [int]($from[1] + ($to[1]-$from[1]) * $i / 12)
    [Native]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 40
  }
  Start-Sleep -Milliseconds 150
  [Native]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 400     # left up
}
if ($c.ContainsKey('from') -and $c.ContainsKey('to')) { Drag $c['from'] $c['to'] }
if ($c.ContainsKey('header')) { Drag $c['header'] @(($c['header'][0] + 140), ($c['header'][1] + 90)) }

$p.WaitForExit(15000) | Out-Null
Write-Output "--- final"
Get-Content "$Prefix.final"
if (-not $p.HasExited) { $p.Kill() }
