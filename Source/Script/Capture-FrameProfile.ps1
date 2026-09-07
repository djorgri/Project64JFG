#Requires -Version 5.1
#Requires -RunAsAdministrator
<#
Capture CPU samples, thread scheduling and GPU activity for an existing game.
Run from an elevated PowerShell after reaching the scene to investigate:
  .\Source\Script\Capture-FrameProfile.ps1 -TargetProcessId 1234
The capture is local. It does not restart or reconfigure the emulator.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [int] $TargetProcessId,
    [ValidateRange(5, 60)]
    [int] $DurationSeconds = 30,
    [ValidateRange(0, 20)]
    [int] $DelaySeconds = 5,
    [string] $OutputDirectory
)

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$target = Get-Process -Id $TargetProcessId
$executable = $target.Path
if (!$executable.StartsWith($repository + '\', [StringComparison]::OrdinalIgnoreCase) -or
    [IO.Path]::GetFileName($executable) -ne 'Project64JFG.exe') {
    throw 'The target must be Project64JFG.exe from this checkout.'
}
$captureId = (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + ([guid]::NewGuid().ToString('N').Substring(0, 8))
if (!$OutputDirectory) { $OutputDirectory = Join-Path $repository "build\profiles\$captureId" }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) { throw 'Choose a new output directory to preserve existing captures.' }
New-Item -ItemType Directory -Path $OutputDirectory | Out-Null
$temporaryDirectory = Join-Path $OutputDirectory 'temporary'
New-Item -ItemType Directory -Path $temporaryDirectory | Out-Null
$sessionName = "Project64JFG-$captureId"
$wpr = Join-Path $env:SystemRoot 'System32\wpr.exe'
$tracePath = Join-Path $OutputDirectory 'frame-profile.etl'
$started = $false
$metadata = [ordered]@{
    state = 'preparing'; process_id = $TargetProcessId; executable = $executable
    process_start_utc = $target.StartTime.ToUniversalTime().ToString('o')
    duration_seconds = $DurationSeconds; session_name = $sessionName
    logical_processors = [Environment]::ProcessorCount; trace = $tracePath
}
function Save-Status {
    $metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'capture.json') -Encoding UTF8
}
function Read-SharedLog([string] $Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
        ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $reader = [IO.StreamReader]::new($stream)
    try {
        while (!$reader.EndOfStream) { $reader.ReadLine() }
    } finally { $reader.Dispose() }
}
function Invoke-Recorder([string[]] $RecorderArguments) {
    # WPR may write progress on stderr even when it succeeds.
    $savedPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $recorderOutput = & $wpr @RecorderArguments 2>&1
    $recorderExit = $LASTEXITCODE
    $ErrorActionPreference = $savedPreference
    $recorderOutput | Out-File -LiteralPath (Join-Path $OutputDirectory 'recorder.log') -Append -Encoding UTF8
    if ($recorderExit -ne 0) { throw "WPR failed with exit code $recorderExit. See recorder.log." }
}

try {
    Save-Status
    $target.Modules | Select-Object ModuleName, FileName, BaseAddress, ModuleMemorySize |
        Export-Csv -LiteralPath (Join-Path $OutputDirectory 'modules.csv') -NoTypeInformation
    Write-Output "Recording starts in $DelaySeconds seconds; keep playing the scene for $DurationSeconds seconds."
    Start-Sleep -Seconds $DelaySeconds
    Invoke-Recorder @('-start', 'CPU', '-start', 'GPU', '-filemode', '-recordtempto', $temporaryDirectory, '-instancename', $sessionName)
    $started = $true
    $logDirectory = Join-Path (Split-Path $executable) 'Logs'
    $logOffsets = @{}
    $metadata.plugin_logs_start_utc = [DateTime]::UtcNow.ToString('o')
    foreach ($name in @('Project64-ParallelRDP.log', 'Project64-ParallelRSP.log', 'Project64-audio.log')) {
        $path = Join-Path $logDirectory $name
        try {
            $logOffsets[$name] = if (Test-Path -LiteralPath $path) { @(Read-SharedLog $path).Count } else { 0 }
        } catch {
            # Plugin logs are optional; a locked log must not prevent ETW capture.
            Write-Warning "Skipping locked log: $name"
        }
    }
    $metadata.state = 'recording'
    $metadata.recording_start_utc = [DateTime]::UtcNow.ToString('o')
    Save-Status
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $samples = [Collections.Generic.List[object]]::new()
    while ($watch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $target.Refresh()
        if ($target.HasExited) { throw 'The emulator exited during the capture.' }
        $elapsed = $watch.Elapsed.TotalSeconds
        $utc = [DateTime]::UtcNow.ToString('o')
        foreach ($thread in $target.Threads) {
            try {
                $state = $thread.ThreadState
                $samples.Add([pscustomobject]@{
                    utc = $utc
                    elapsed_seconds = $elapsed.ToString('R', [Globalization.CultureInfo]::InvariantCulture)
                    thread_id = $thread.Id
                    cpu_ms = $thread.TotalProcessorTime.TotalMilliseconds.ToString('R', [Globalization.CultureInfo]::InvariantCulture)
                    state = $state.ToString()
                    wait_reason = if ($state -eq [Diagnostics.ThreadState]::Wait) { $thread.WaitReason.ToString() } else { '' }
                })
            } catch { } # Threads can terminate between enumeration and reading.
        }
        Start-Sleep -Milliseconds 1000
    }
    $metadata.recording_end_utc = [DateTime]::UtcNow.ToString('o')
    $metadata.measured_seconds = $watch.Elapsed.TotalSeconds
    $samples | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'thread-times.csv') -NoTypeInformation
    $metadata.state = 'saving'
    Save-Status
    # Snapshot logs before WPR's potentially lengthy merge/rundown. Plugin
    # counters cover reporting intervals that can straddle these boundaries.
    foreach ($name in $logOffsets.Keys) {
        $path = Join-Path $logDirectory $name
        if (Test-Path -LiteralPath $path) {
            try {
                $lines = @(Read-SharedLog $path)
                $offset = if ($lines.Length -ge $logOffsets[$name]) { $logOffsets[$name] } else { 0 }
                $lines | Select-Object -Skip $offset | Set-Content -LiteralPath (Join-Path $OutputDirectory $name) -Encoding UTF8
            } catch { Write-Warning "Could not copy plugin log: $name" }
        }
    }
    $metadata.plugin_logs_end_utc = [DateTime]::UtcNow.ToString('o')
    Save-Status
    Invoke-Recorder @('-stop', $tracePath, 'Jet Force Gemini frame production slowdown', '-skipPdbGen', '-instancename', $sessionName)
    $started = $false
    $metadata.state = 'complete'
    Save-Status
    Write-Output "Capture saved to $OutputDirectory"
} catch {
    $metadata.state = 'failed'
    $metadata.error = $_.Exception.Message
    Save-Status
    throw
} finally {
    if ($started) {
        # Stop only the unique session created by this invocation.
        try { Invoke-Recorder @('-stop', $tracePath, '-skipPdbGen', '-instancename', $sessionName) } catch { }
    }
}
