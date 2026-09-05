param(
    [string]$Report = ".\\artifacts\\windows-hardware.json",
    [string]$OutFile = ".\\artifacts\\target.json"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

if (!(Test-Path $Report)) {
    throw "Hardware report not found: $Report. Run collect-hardware.ps1 first."
}

$data = Get-Content -Raw -Path $Report | ConvertFrom-Json
$candidates = @()

foreach ($display in @($data.DisplayDevices)) {
    foreach ($hardwareId in @($display.HardwareIds)) {
        if ($null -eq $hardwareId) { continue }
        $text = [string]$hardwareId
        if ($text -match '^PCI\\VEN_([0-9A-Fa-f]{4})&DEV_([0-9A-Fa-f]{4})(?:&SUBSYS_([0-9A-Fa-f]{8}))?') {
            $vendor = $Matches[1].ToUpperInvariant()
            $device = $Matches[2].ToUpperInvariant()
            $subsystemDevice = $null
            $subsystemVendor = $null
            if ($Matches[3]) {
                $subsystemDevice = $Matches[3].Substring(0, 4).ToUpperInvariant()
                $subsystemVendor = $Matches[3].Substring(4, 4).ToUpperInvariant()
            }
            $candidates += [pscustomobject]@{
                FriendlyName = [string]$display.FriendlyName
                InstanceId = [string]$display.InstanceId
                HardwareId = $text
                Vendor = $vendor
                Device = $device
                SubsystemVendor = $subsystemVendor
                SubsystemDevice = $subsystemDevice
            }
            break
        }
    }
}

$nvidia = @($candidates | Where-Object { $_.Vendor -eq '10DE' })
if ($nvidia.Count -eq 0) {
    throw "No NVIDIA display-class PCI device was found in the report."
}
if ($nvidia.Count -ne 1) {
    $names = ($nvidia | ForEach-Object { "$($_.FriendlyName) [$($_.HardwareId)]" }) -join '; '
    throw "Expected exactly one NVIDIA display device, found $($nvidia.Count): $names"
}

$gpu = $nvidia[0]
$primaryMatch = "0x$($gpu.Device.ToLowerInvariant())$($gpu.Vendor.ToLowerInvariant())"

$target = [ordered]@{
    Schema = "rtxmac.target.v1"
    FriendlyName = $gpu.FriendlyName
    InstanceId = $gpu.InstanceId
    HardwareId = $gpu.HardwareId
    Vendor = "0x$($gpu.Vendor)"
    Device = "0x$($gpu.Device)"
    SubsystemVendor = if ($gpu.SubsystemVendor) { "0x$($gpu.SubsystemVendor)" } else { $null }
    SubsystemDevice = if ($gpu.SubsystemDevice) { "0x$($gpu.SubsystemDevice)" } else { $null }
    IOPCIPrimaryMatch = $primaryMatch
}

$directory = Split-Path -Parent $OutFile
if ($directory -and !(Test-Path $directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$target | ConvertTo-Json -Depth 5 | Set-Content -Path $OutFile -Encoding UTF8

Write-Host "RTXMac target prepared:"
Write-Host "  Name:               $($gpu.FriendlyName)"
Write-Host "  PCI:                $($gpu.Vendor):$($gpu.Device)"
Write-Host "  IOPCIPrimaryMatch:  $primaryMatch"
Write-Host "  Target file:        $OutFile"
Write-Host "No GPU registers were accessed by this script."
