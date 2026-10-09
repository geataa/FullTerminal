param(
    [string]$Source = (Join-Path $PSScriptRoot '..\bin\FullTerminal.exe'),
    [switch]$Publishing
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$cmake = Get-Content (Join-Path $repo 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(FullTerminal VERSION (\d+\.\d+\.\d+)\b') {
    throw 'Cannot read the project version.'
}
$version = $Matches[1]
$tag = "v$version"
$header = Get-Content (Join-Path $repo 'src\core\Version.h') -Raw
if ($header -notmatch ('#define FT_VERSION "' + [regex]::Escape($version) + '"')) {
    throw 'CMake and runtime versions disagree.'
}
[xml]$manifest = Get-Content (Join-Path $repo 'resources\app.manifest') -Raw
if ($manifest.assembly.assemblyIdentity.version -ne "$version.0") {
    throw 'Manifest and project versions disagree.'
}
$binary = Get-Item -LiteralPath $Source
if ($binary.Name -ne 'FullTerminal.exe' -or
    $binary.VersionInfo.FileVersion -ne "$version.0" -or
    $binary.VersionInfo.ProductVersion -ne "$version.0") {
    throw 'Expected FullTerminal.exe with matching file and product versions.'
}
if ($Publishing) {
    if ($env:GITHUB_REF_TYPE -eq 'tag' -and $env:GITHUB_REF_NAME -ne $tag) {
        throw "Release tag must match $tag."
    }
    $head = git -C $repo rev-parse HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read HEAD.' }
    $tagCommit = git -C $repo rev-parse --verify "$tag^{commit}"
    if ($LASTEXITCODE -ne 0 -or $tagCommit -ne $head) {
        throw "Release requires an existing $tag tag pointing to this commit."
    }
}
$notes = Join-Path $repo "docs\releases\$tag.md"
if (-not (Test-Path -LiteralPath $notes)) { throw "Missing release notes: $tag" }

# A fresh staging folder prevents local settings or stale build files entering the archive.
$dist = Join-Path $repo 'dist'
$stage = Join-Path $dist ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage -Force | Out-Null
Copy-Item -LiteralPath $binary.FullName -Destination (Join-Path $stage 'FullTerminal.exe')
Copy-Item -LiteralPath (Join-Path $repo 'README.md'), (Join-Path $repo 'LICENSE') -Destination $stage
Copy-Item -LiteralPath $notes -Destination (Join-Path $stage 'RELEASE-NOTES.md')
Copy-Item -LiteralPath (Join-Path $repo 'vendor\sqlite\README.md') -Destination (Join-Path $stage 'SQLITE-NOTICE.md')
$zipName = "FullTerminal-$tag-windows-x64.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $dist $zipName) -Force
Copy-Item -LiteralPath (Join-Path $dist $zipName) -Destination (Join-Path $dist 'FullTerminal-Portable-x64.zip')
Copy-Item -LiteralPath $binary.FullName -Destination (Join-Path $dist 'FullTerminal.exe')
$checksums = foreach ($name in @('FullTerminal.exe', $zipName, 'FullTerminal-Portable-x64.zip')) {
    $hash = (Get-FileHash -LiteralPath (Join-Path $dist $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $name"
}
$checksums | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii
if ($env:GITHUB_ENV) {
    "RELEASE_TAG=$tag", "ZIP_NAME=$zipName" | Add-Content -LiteralPath $env:GITHUB_ENV -Encoding utf8
}
Write-Output "Packaged $tag ($($binary.VersionInfo.FileVersion)): $dist\$zipName"
