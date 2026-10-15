[CmdletBinding()]
param(
    [string]$Executable = "$env:LOCALAPPDATA\Player\player.exe",
    [string]$Server,
    [string]$Name = 'Player',
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$taskName = "LyrionPlayer-$($identity.User.Value)"
if ($Uninstall) {
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $taskName
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    }
    Write-Output 'Player startup removed. Executable and logs retained.'
    return
}
$binary = (Resolve-Path -LiteralPath $Executable).Path
$logDirectory = Join-Path $env:LOCALAPPDATA 'Player'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
# Quote with the Windows CRT argv rules, including trailing backslashes.
function Quote-Argument([string]$Value) {
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
$playerArguments = @('-n', $Name, '-d', 'output=info', '-f', (Join-Path $logDirectory 'player.log'))
if ($Server) { $playerArguments += @('-s', $Server) }
$argumentString = ($playerArguments | ForEach-Object { Quote-Argument $_ }) -join ' '
$action = New-ScheduledTaskAction -Execute $binary -Argument $argumentString -WorkingDirectory (Split-Path $binary)
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $identity.Name
$principal = New-ScheduledTaskPrincipal -UserId $identity.Name -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero) -RestartCount 999 -RestartInterval (New-TimeSpan -Minutes 1) `
    -MultipleInstances IgnoreNew -StartWhenAvailable
if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
    Stop-ScheduledTask -TaskName $taskName
}
Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings -Force | Out-Null
Start-ScheduledTask -TaskName $taskName
Write-Output "Player starts now and at sign-in. Logs: $logDirectory\player.log"
