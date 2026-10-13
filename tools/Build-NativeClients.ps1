$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$native = Join-Path $root 'vendor/native'
$tool = Join-Path $root 'build/native-vcpkg'
$installed = Join-Path $root 'build/native-installed'
$baseline = 'e456309491fd875487bf02b8a0dca76c1b1b00cc'
$env:VCPKG_DISABLE_METRICS = '1'
$env:VCPKG_BINARY_SOURCES = 'clear;files,' + (Join-Path $root 'build/native-cache') + ',readwrite'
if (!(Test-Path -LiteralPath (Join-Path $tool '.git'))) {
    New-Item -ItemType Directory -Force -Path $tool | Out-Null
    & git -C $tool init --quiet
    if ($LASTEXITCODE) { throw 'Could not create the local build-tool checkout.' }
    & git -C $tool remote add origin https://github.com/microsoft/vcpkg.git
    if ($LASTEXITCODE) { throw 'Could not configure the build-tool origin.' }
    & git -C $tool fetch --depth=1 origin $baseline
    if ($LASTEXITCODE) { throw 'Could not download the pinned build tool.' }
}
& git -C $tool checkout --detach $baseline
if ($LASTEXITCODE) { throw 'Could not select the pinned vcpkg revision.' }
if (!(Test-Path -LiteralPath (Join-Path $tool 'vcpkg.exe'))) {
    & (Join-Path $tool 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE) { throw 'Could not initialize the local build tool.' }
}
$downloads = Join-Path $tool 'downloads'
New-Item -ItemType Directory -Force -Path $downloads | Out-Null
Get-ChildItem -LiteralPath $native -File | Where-Object { $_.Name -match '\.tar\.(gz|bz2|xz)$' } | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $downloads -Force
}
& (Join-Path $tool 'vcpkg.exe') install "--x-manifest-root=$native" "--x-install-root=$installed" "--overlay-ports=$native/ports" "--overlay-triplets=$native/triplets" --triplet=x64-ft-static
if ($LASTEXITCODE) { throw 'Static database client build failed.' }
Write-Host 'Static client libraries ready. No database servers or system drivers were installed.'
