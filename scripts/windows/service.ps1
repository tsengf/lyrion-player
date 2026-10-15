[CmdletBinding()]
param(
    [string]$Executable = "$env:LOCALAPPDATA\Player\player.exe",
    [string]$Server,
    [string]$Name = 'Player',
    [ValidateNotNullOrEmpty()]
    [ValidatePattern('(?-i)\A[a-z0-9][a-z0-9_-]{0,63}\z')]
    [string]$Instance,
    [ValidateNotNullOrEmpty()]
    [ValidatePattern('\A(?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\z')]
    [string]$Mac,
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
if ($Instance -and -not $Uninstall -and -not $Mac) {
    throw '-Mac is required with -Instance; assign a unique identifier to each player.'
}
if ($Mac) {
    if ($Mac -eq '00:00:00:00:00:00' -or ([Convert]::ToByte($Mac.Substring(0, 2), 16) -band 1)) {
        throw 'Use a nonzero unicast player identifier.'
    }
    $Mac = $Mac.ToLowerInvariant()
}
$identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
$taskName = "LyrionPlayer-$($identity.User.Value)"
if ($Instance) { $taskName += "-$Instance" }
if ($Uninstall) {
    if (Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue) {
        Stop-ScheduledTask -TaskName $taskName
        Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
    }
    Write-Output "Removed $taskName. Executable and logs retained."
    return
}
$binary = (Resolve-Path -LiteralPath $Executable).Path
$logDirectory = Join-Path $env:LOCALAPPDATA 'lyrion-player'
New-Item -ItemType Directory -Force -Path $logDirectory | Out-Null
$logFileName = if ($Instance) { "player-$Instance.log" } else { 'player.log' }
$logFile = Join-Path $logDirectory $logFileName
# Quote with the Windows CRT argv rules, including trailing backslashes.
function Quote-Argument([string]$Value) {
    '"' + [regex]::Replace([regex]::Replace($Value, '(\\*)"', '$1$1\"'), '(\\+)$', '$1$1') + '"'
}
$playerArguments = @('-n', $Name, '-d', 'output=info', '-f', $logFile)
if ($Server) { $playerArguments += @('-s', $Server) }
if ($Mac) { $playerArguments += @('-m', $Mac) }
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
Write-Output "$taskName starts now and at sign-in. Logs: $logFile"
