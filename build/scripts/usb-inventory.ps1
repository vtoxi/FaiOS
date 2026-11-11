# Read-only USB inventory for hardware discovery. Lists connected/known USB devices with VID/PID.
# Run it with the phone unplugged, then plugged in (and in each boot state); diff the outputs.
# It only reads Windows' device database. It never talks to the phone.
#   powershell -ExecutionPolicy Bypass -File build\scripts\usb-inventory.ps1 [-Out file.txt]
param([string]$Out = "")

$rows = Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue |
  Where-Object { $_.InstanceId -like 'USB\*' } |
  ForEach-Object {
    $vid = if ($_.InstanceId -match 'VID_([0-9A-F]{4})') { $Matches[1] } else { '' }
    $pid_ = if ($_.InstanceId -match 'PID_([0-9A-F]{4})') { $Matches[1] } else { '' }
    [pscustomobject]@{ VID = $vid; PID = $pid_; Class = $_.Class; Status = $_.Status; Name = $_.FriendlyName; InstanceId = $_.InstanceId }
  } | Sort-Object VID, PID

$text = ($rows | Format-Table -AutoSize -Wrap | Out-String)
$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
$result = "USB inventory $stamp`n$text"
if ($Out) { $result | Set-Content -Path $Out -Encoding utf8; "Saved to $Out" } else { $result }
