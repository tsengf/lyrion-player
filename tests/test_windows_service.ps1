# Run on Windows; task and filesystem operations are replaced by in-memory fakes.
$ErrorActionPreference = 'Stop'
$installer = Join-Path $PSScriptRoot '..\scripts\windows\service.ps1'
$global:PlayerServiceTestTasks = @{}
$global:PlayerServiceTestResolveCalls = 0

function Resolve-Path {
    param([string]$LiteralPath)
    $global:PlayerServiceTestResolveCalls++
    [pscustomobject]@{ Path = $LiteralPath }
}
function New-Item { param($ItemType, [switch]$Force, $Path) }
function New-ScheduledTaskAction {
    param($Execute, $Argument, $WorkingDirectory)
    [pscustomobject]@{ Execute = $Execute; Argument = $Argument; WorkingDirectory = $WorkingDirectory }
}
function New-ScheduledTaskTrigger {
    param([switch]$AtLogOn, $User)
    [pscustomobject]@{ User = $User; AtLogOn = [bool]$AtLogOn }
}
function New-ScheduledTaskPrincipal {
    param($UserId, $LogonType, $RunLevel)
    [pscustomobject]@{ UserId = $UserId; LogonType = $LogonType; RunLevel = $RunLevel }
}
function New-ScheduledTaskSettingsSet {
    param([switch]$AllowStartIfOnBatteries, [switch]$DontStopIfGoingOnBatteries,
          $ExecutionTimeLimit, $RestartCount, $RestartInterval, $MultipleInstances, [switch]$StartWhenAvailable)
    [pscustomobject]@{ RestartCount = $RestartCount; MultipleInstances = $MultipleInstances }
}
function Get-ScheduledTask {
    [CmdletBinding()]
    param($TaskName)
    $global:PlayerServiceTestTasks[$TaskName]
}
function Register-ScheduledTask {
    param($TaskName, $Action, $Trigger, $Principal, $Settings, [switch]$Force)
    $global:PlayerServiceTestTasks[$TaskName] = [pscustomobject]@{
        Action = $Action; Trigger = $Trigger; Principal = $Principal; Settings = $Settings; Started = $false
    }
}
function Start-ScheduledTask {
    param($TaskName)
    $global:PlayerServiceTestTasks[$TaskName].Started = $true
}
function Stop-ScheduledTask {
    param($TaskName)
    $global:PlayerServiceTestTasks[$TaskName].Started = $false
}
function Unregister-ScheduledTask {
    param($TaskName, [switch]$Confirm)
    $global:PlayerServiceTestTasks.Remove($TaskName)
}
function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw $Message }
}
function Assert-Fails {
    param([scriptblock]$Command)
    $failed = $false
    try { & $Command | Out-Null } catch { $failed = $true }
    Assert-True $failed 'Invalid arguments were accepted'
}

try {
    $binary = 'C:\Player & Music\player.exe'
    $default = "LyrionPlayer-$([System.Security.Principal.WindowsIdentity]::GetCurrent().User.Value)"
    & $installer -Executable $binary -Instance server-a -Name 'My PC - Server A' `
        -Server 192.168.1.10 -Mac 02:00:00:00:00:01 | Out-Null
    & $installer -Executable $binary -Instance server-b -Server 192.168.1.20 `
        -Mac 02:00:00:00:00:02 | Out-Null
    $a = $global:PlayerServiceTestTasks["$default-server-a"]
    $b = $global:PlayerServiceTestTasks["$default-server-b"]
    Assert-True ($global:PlayerServiceTestTasks.Count -eq 2) 'Instances overwrote each other'
    Assert-True ($a.Started -and $b.Started) 'Both instances must start'
    Assert-True ($a.Action.Execute -eq $binary) 'Executable path was not preserved'
    Assert-True ($a.Action.Argument.Contains('"My PC - Server A"')) 'Player name was not quoted'
    Assert-True ($a.Action.Argument.Contains('"-m" "02:00:00:00:00:01"')) 'Player identity was not passed'
    Assert-True ($a.Action.Argument.Contains('\lyrion-player\player-server-a.log')) 'Instance log path is incorrect'
    Assert-True ($a.Principal.LogonType -eq 'Interactive') 'Task must use the user session'

    & $installer -Executable $binary -Instance server-a -Server 192.168.1.30 `
        -Mac 02:00:00:00:00:01 | Out-Null
    Assert-True ($global:PlayerServiceTestTasks["$default-server-a"].Action.Argument.Contains('192.168.1.30')) `
        'Instance was not updated'
    Assert-True ([object]::ReferenceEquals($b, $global:PlayerServiceTestTasks["$default-server-b"])) `
        'Updating Server A modified Server B'
    $global:PlayerServiceTestResolveCalls = 0
    & $installer -Instance server-a -Uninstall | Out-Null
    Assert-True (-not $global:PlayerServiceTestTasks.ContainsKey("$default-server-a")) 'Server A was not removed'
    Assert-True $b.Started 'Removing Server A stopped Server B'
    Assert-True ($global:PlayerServiceTestResolveCalls -eq 0) 'Uninstall requires an executable'

    & $installer -Executable $binary | Out-Null
    $legacy = $global:PlayerServiceTestTasks[$default]
    Assert-True ($legacy.Action.Argument.Contains('\lyrion-player\player.log')) 'Default log path changed'
    Assert-True (-not $legacy.Action.Argument.Contains('"-m"')) 'Default identity changed'
    & $installer -Uninstall | Out-Null
    Assert-True (-not $global:PlayerServiceTestTasks.ContainsKey($default)) 'Default task was not removed'
    Assert-True $b.Started 'Removing default task stopped a named instance'

    Assert-Fails { & $installer -Instance '../bad' -Mac 02:00:00:00:00:01 }
    Assert-Fails { & $installer -Instance 'Server-A' -Mac 02:00:00:00:00:01 }
    Assert-Fails { & $installer -Instance '' -Uninstall }
    Assert-Fails { & $installer -Instance server-a }
    Assert-Fails { & $installer -Mac invalid }
    Assert-Fails { & $installer -Mac 00:00:00:00:00:00 }
    Assert-Fails { & $installer -Mac 01:00:00:00:00:01 }
    Assert-True ($global:PlayerServiceTestTasks.Count -eq 1 -and $b.Started) 'Invalid input modified tasks'
    Write-Output 'Windows service checks passed; no real tasks or files were modified.'
} finally {
    Remove-Variable PlayerServiceTestTasks, PlayerServiceTestResolveCalls -Scope Global
}
