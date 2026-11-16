param(
    [Parameter(Mandatory = $true)][string]$Source,
    [string]$Destination = ''
)
$ErrorActionPreference = 'Stop'
# Avoid module autoload differences when Windows PowerShell is launched from pwsh.
function Get-BinaryHash([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '')
    } finally {
        $sha.Dispose()
        $stream.Dispose()
    }
}
$projectRoot = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
if (-not $Destination) { $Destination = Join-Path $projectRoot 'FullTerminal.exe' }
$sourcePath = [IO.Path]::GetFullPath($Source)
$destinationPath = [IO.Path]::GetFullPath($Destination)
foreach ($path in @($sourcePath, $destinationPath)) {
    if (-not $path.StartsWith($projectRoot + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Binary publishing is restricted to this workspace.'
    }
}
if ([IO.Path]::GetFileName($destinationPath) -ne 'FullTerminal.exe') {
    throw 'The published executable must be named FullTerminal.exe.'
}
if ($sourcePath -eq $destinationPath) { throw 'Source and destination must differ.' }
$expectedHash = Get-BinaryHash $sourcePath
# A clean checkout has no ignored bin directory yet.
New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($destinationPath)) -Force | Out-Null
$backupPath = $null
if (Test-Path -LiteralPath $destinationPath) {
    if ((Get-BinaryHash $destinationPath) -eq $expectedHash) {
        Write-Host "Already current: $destinationPath"
        exit 0
    }
    # Windows permits renaming an executable while its image is in use. Keep
    # the old image under build, with the same basename, until its process exits.
    $backupDirectory = Join-Path $projectRoot ('build\binary-backups\' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backupDirectory -Force | Out-Null
    $backupPath = Join-Path $backupDirectory 'FullTerminal.exe'
    Move-Item -LiteralPath $destinationPath -Destination $backupPath
}
try {
    Copy-Item -LiteralPath $sourcePath -Destination $destinationPath
    if ((Get-BinaryHash $destinationPath) -ne $expectedHash) {
        throw 'Published binary hash does not match the build.'
    }
} catch {
    if (Test-Path -LiteralPath $destinationPath) { Remove-Item -LiteralPath $destinationPath }
    if ($backupPath) { Move-Item -LiteralPath $backupPath -Destination $destinationPath }
    throw
}
Write-Host "Published: $destinationPath"
if ($backupPath) { Write-Host 'An already-running application keeps its old code until it is restarted.' }
