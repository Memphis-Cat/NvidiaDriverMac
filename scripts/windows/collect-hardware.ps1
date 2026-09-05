param(
    [string]$OutFile = ".\\artifacts\\windows-hardware.json"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Try-Property {
    param([string]$InstanceId, [string]$KeyName)
    try {
        return (Get-PnpDeviceProperty -InstanceId $InstanceId -KeyName $KeyName -ErrorAction Stop).Data
    } catch {
        return $null
    }
}

$displayDevices = @(Get-PnpDevice -Class Display -PresentOnly | ForEach-Object {
    $instanceId = $_.InstanceId
    [ordered]@{
        FriendlyName = $_.FriendlyName
        Status = $_.Status
        InstanceId = $instanceId
        HardwareIds = @(Try-Property $instanceId "DEVPKEY_Device_HardwareIds")
        LocationInfo = Try-Property $instanceId "DEVPKEY_Device_LocationInfo"
        BusNumber = Try-Property $instanceId "DEVPKEY_Device_BusNumber"
        Address = Try-Property $instanceId "DEVPKEY_Device_Address"
        DriverVersion = Try-Property $instanceId "DEVPKEY_Device_DriverVersion"
    }
})

$nvidiaSmi = $null
if (Get-Command nvidia-smi -ErrorAction SilentlyContinue) {
    try {
        $nvidiaSmi = @(& nvidia-smi --query-gpu=name,pci.device_id,pci.bus_id,driver_version,vbios_version,memory.total --format=csv,noheader,nounits 2>$null)
    } catch {
        $nvidiaSmi = @("nvidia-smi failed: $($_.Exception.Message)")
    }
}

$report = [ordered]@{
    Schema = "rtxmac.windows-hardware.v1"
    CapturedAtUtc = [DateTime]::UtcNow.ToString("o")
    ComputerSystem = Get-CimInstance Win32_ComputerSystem | Select-Object Manufacturer, Model, TotalPhysicalMemory
    BaseBoard = Get-CimInstance Win32_BaseBoard | Select-Object Manufacturer, Product, Version
    BIOS = Get-CimInstance Win32_BIOS | Select-Object Manufacturer, SMBIOSBIOSVersion, ReleaseDate
    CPU = @(Get-CimInstance Win32_Processor | Select-Object Name, Manufacturer, NumberOfCores, NumberOfLogicalProcessors, ProcessorId)
    DisplayDevices = $displayDevices
    NvidiaSmi = $nvidiaSmi
}

$directory = Split-Path -Parent $OutFile
if ($directory -and !(Test-Path $directory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

$report | ConvertTo-Json -Depth 8 | Set-Content -Path $OutFile -Encoding UTF8
Write-Host "Wrote RTXMac hardware report to $OutFile"
Write-Host "This script only reads Windows hardware metadata; it does not touch GPU registers."
