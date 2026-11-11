# Read-only USB watcher: prints a timestamped line whenever a USB device appears or disappears.
# Use it while plugging the phone in (and while holding any documented boot-mode key) to record
# how it enumerates. It only reads Windows' device list; it never talks to the phone.
# Polls about once per second, so a device that exists for under a second can be missed.
#   powershell -ExecutionPolicy Bypass -File build\scripts\usb-watch.ps1 [-Seconds 120] [-Out log.txt]
param([int]$Seconds = 120, [string]$Out = "")

function Snapshot {
  $h = @{}
  Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue | Where-Object { $_.InstanceId -like 'USB\VID_*' } |
    ForEach-Object { $h[$_.InstanceId] = "$($_.FriendlyName) [$($_.Status)]" }
  $h
}

function Log([string]$line) {
  Write-Output $line
  if ($Out) { Add-Content -Path $Out -Value $line -Encoding utf8 }
}

$prev = Snapshot
Log ("{0} watching {1}s ({2} USB devices present)" -f (Get-Date -Format 'HH:mm:ss'), $Seconds, $prev.Count)
$end = (Get-Date).AddSeconds($Seconds)
while ((Get-Date) -lt $end) {
  Start-Sleep -Milliseconds 700
  $cur = Snapshot
  foreach ($k in $cur.Keys) {
    if (-not $prev.ContainsKey($k)) { Log ("{0} + {1}  {2}" -f (Get-Date -Format 'HH:mm:ss.fff'), $k, $cur[$k]) }
    elseif ($prev[$k] -ne $cur[$k]) { Log ("{0} ~ {1}  {2} (was {3})" -f (Get-Date -Format 'HH:mm:ss.fff'), $k, $cur[$k], $prev[$k]) }
  }
  foreach ($k in $prev.Keys) {
    if (-not $cur.ContainsKey($k)) { Log ("{0} - {1}  {2}" -f (Get-Date -Format 'HH:mm:ss.fff'), $k, $prev[$k]) }
  }
  $prev = $cur
}
Log ("{0} done" -f (Get-Date -Format 'HH:mm:ss'))
