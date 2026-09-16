[CmdletBinding()]
param(
    [string] $OutputDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot 'Package'
}

$projectName = 'Project64JFG'
$binDirectory = Join-Path $PSScriptRoot 'bin'
$configTemplate = Join-Path $PSScriptRoot 'Config\Project64.cfg.development'

$builds = @(
    @{ Platform = 'x64';   Configuration = 'Release'; Suffix = 'win64' },
    @{ Platform = 'Win32'; Configuration = 'Release'; Suffix = 'win32' },
    @{ Platform = 'x64';   Configuration = 'Debug';   Suffix = 'win64-debug' },
    @{ Platform = 'Win32'; Configuration = 'Debug';   Suffix = 'win32-debug' }
)

function Remove-BuildArtifacts {
    param([Parameter(Mandatory)][string] $Directory)

    Get-ChildItem -LiteralPath $Directory -Recurse -Force -File |
        Where-Object { $_.Extension -in '.exp', '.ilk', '.lib', '.map', '.pdb' } |
        Remove-Item -Force

    Get-ChildItem -LiteralPath $Directory -Recurse -Force -Directory |
        Where-Object { $_.Name -in 'lib', 'map', 'pdb' } |
        Sort-Object FullName -Descending |
        Remove-Item -Recurse -Force
}

function Remove-RuntimeData {
    param([Parameter(Mandatory)][string] $Directory)

    # Running the emulator from bin\<platform>\<configuration> fills it with
    # personal data: save states, logs, screenshots, probe dumps, caches. None
    # of that belongs in a distribution, and the emulator recreates the
    # directories it needs on first launch.
    foreach ($name in 'Logs', 'Roms', 'Save', 'Screenshots', 'Scripts', 'Textures') {
        $path = Join-Path $Directory $name
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Recurse -Force
        }
    }

    Get-ChildItem -LiteralPath $Directory -Force -File |
        Where-Object { $_.Extension -in '.log', '.dump' } |
        Remove-Item -Force

    # The Parallel-RDP plugin keeps its settings next to its DLL and falls
    # back to the defaults when the file is absent.
    $rdpSettings = Join-Path $Directory 'Plugin\GFX\Project64-ParallelRDP.ini'
    if (Test-Path -LiteralPath $rdpSettings) {
        Remove-Item -LiteralPath $rdpSettings -Force
    }

    $configDirectory = Join-Path $Directory 'Config'
    if (Test-Path -LiteralPath $configDirectory) {
        Get-ChildItem -LiteralPath $configDirectory -Force -File |
            Where-Object { $_.Extension -in '.cache3', '.rdn', '.sc3', '.zcache' } |
            Remove-Item -Force

        foreach ($name in 'Cheats-User', 'Enhancements-User') {
            $path = Join-Path $configDirectory $name
            if (Test-Path -LiteralPath $path) {
                Remove-Item -LiteralPath $path -Recurse -Force
            }
        }
    }
}

function Reset-DistributionConfiguration {
    param([Parameter(Mandatory)][string] $ConfigFile)

    # The development template is used from bin\<platform>\<configuration>.
    # A release instead runs from its own root, so every path must be relative
    # to that root for the ROM database and compatibility settings to load.
    $portablePaths = [ordered]@{
        '7zipCache'            = 'Config\Project64.zcache'
        'AudioRDB'              = 'Config\Audio.rdb'
        'CheatDir'              = 'Config\Cheats\'
        'EnhancementDir'        = 'Config\Enhancements\'
        'ExtInfo'               = 'Config\Project64.rdx'
        'Notes'                 = 'Config\Project64.rdn'
        'RomDatabase'           = 'Config\Project64.rdb'
        'RomListCache'          = 'Config\Project64.cache3'
        'ShortCuts'             = 'Config\Project64.sc3'
        'UserCheatDir'          = 'Config\Cheats-User\'
        'UserEnhancementDir'    = 'Config\Enhancements-User\'
        'VideoRDB'              = 'Config\Video.rdb'
    }
    $keyExpression = '^(' + (($portablePaths.Keys | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')='

    $lines = Get-Content -LiteralPath $ConfigFile
    $cleanLines = foreach ($line in $lines) {
        if ($line -match $keyExpression) {
            "$($matches[1])=$($portablePaths[$matches[1]])"
            continue
        }

        if ($line -eq 'Directory=..\..\..\Lang') {
            'Directory=Lang'
            continue
        }

        $line
    }

    [System.IO.File]::WriteAllLines($ConfigFile, [string[]] $cleanLines, [System.Text.UTF8Encoding]::new($true))
}

function Copy-OptionalPath {
    param(
        [Parameter(Mandatory)][string] $Source,
        [Parameter(Mandatory)][string] $Destination
    )

    if (Test-Path -LiteralPath $Source) {
        Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
    }
}

if (-not (Get-Command Compress-Archive -ErrorAction SilentlyContinue)) {
    throw 'Compress-Archive is required. Run this script in Windows PowerShell 5.1 or newer.'
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$createdArchives = [System.Collections.Generic.List[string]]::new()
foreach ($build in $builds) {
    $source = Join-Path $binDirectory "$($build.Platform)\$($build.Configuration)"
    $exe = Join-Path $source "$projectName.exe"
    $requiredPaths = @(
        $exe,
        (Join-Path $source 'Config\Project64.cfg'),
        (Join-Path $source 'Plugin\Audio\Project64-Audio.dll'),
        (Join-Path $source 'Plugin\Input\Project64-Input.dll'),
        (Join-Path $source 'Plugin\GFX\Project64-ParallelRDP.dll'),
        (Join-Path $source 'Plugin\RSP\Project64-ParallelRSP.dll')
    )

    $missingPaths = $requiredPaths | Where-Object { -not (Test-Path -LiteralPath $_) }
    if ($missingPaths) {
        Write-Warning "Skipping $($build.Platform) $($build.Configuration): incomplete export."
        $missingPaths | ForEach-Object { Write-Warning "  Missing: $_" }
        continue
    }

    $version = (Get-Item -LiteralPath $exe).VersionInfo.ProductVersion
    if ([string]::IsNullOrWhiteSpace($version)) {
        throw "Unable to read the version from $exe."
    }
    $version = ($version -replace '[^0-9.]', '').Trim('.')
    if ([string]::IsNullOrWhiteSpace($version)) {
        throw "Unable to normalize the version from $exe."
    }

    $archiveName = "$projectName-$version-$($build.Suffix).zip"
    $archivePath = Join-Path $OutputDirectory $archiveName
    $stagingDirectory = Join-Path $OutputDirectory ".staging-$($build.Platform)-$($build.Configuration)"
    $packageRootName = [System.IO.Path]::GetFileNameWithoutExtension($archiveName)
    $packageRoot = Join-Path $stagingDirectory $packageRootName

    if (Test-Path -LiteralPath $stagingDirectory) {
        Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
    }

    try {
        New-Item -ItemType Directory -Path $stagingDirectory | Out-Null
        New-Item -ItemType Directory -Path $packageRoot | Out-Null
        Copy-Item -Path (Join-Path $source '*') -Destination $packageRoot -Recurse -Force
        Remove-BuildArtifacts -Directory $packageRoot
        Remove-RuntimeData -Directory $packageRoot

        Copy-Item -LiteralPath $configTemplate -Destination (Join-Path $packageRoot 'Config\Project64.cfg') -Force
        Reset-DistributionConfiguration -ConfigFile (Join-Path $packageRoot 'Config\Project64.cfg')

        Copy-OptionalPath -Source (Join-Path $PSScriptRoot 'Config\Cheats') -Destination (Join-Path $packageRoot 'Config')
        Copy-OptionalPath -Source (Join-Path $PSScriptRoot 'Config\Enhancements') -Destination (Join-Path $packageRoot 'Config')
        Copy-OptionalPath -Source (Join-Path $PSScriptRoot 'Lang') -Destination $packageRoot
        Copy-OptionalPath -Source (Join-Path $PSScriptRoot 'Licenses') -Destination $packageRoot
        Copy-OptionalPath -Source (Join-Path $PSScriptRoot 'license.md') -Destination $packageRoot

        Compress-Archive -Path $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal -Force
        $createdArchives.Add($archivePath)
        Write-Host "Created: $archivePath"
    }
    finally {
        if (Test-Path -LiteralPath $stagingDirectory) {
            Remove-Item -LiteralPath $stagingDirectory -Recurse -Force
        }
    }
}

if ($createdArchives.Count -eq 0) {
    throw 'No complete export was found under bin. Build Release|x64 (or another configuration) first.'
}

Write-Host "Created $($createdArchives.Count) release package(s) in $OutputDirectory."
