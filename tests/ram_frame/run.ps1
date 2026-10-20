# Run from an x64 MSVC developer PowerShell with VCPKG_ROOT set.
$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot/../..").Path
Push-Location $repo
try {
    $ErrorActionPreference = 'Continue'
    $build = @(& cargo +stable build --release --offline --message-format=json)
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE -ne 0) { throw 'Building hwcodec failed.' }
    $native = $build | ForEach-Object { $_ | ConvertFrom-Json } |
        Where-Object { $_.reason -eq 'build-script-executed' -and (Test-Path (Join-Path $_.out_dir 'hwcodec.lib')) } |
        Select-Object -Last 1
    if (-not $native) { throw 'Native library not found.' }
    $vcpkg = Join-Path $env:VCPKG_ROOT 'installed/x64-windows-static'
    $outDir = (New-Item -ItemType Directory -Force 'target/ram-frame').FullName
    $exe = Join-Path $outDir 'owned_frame.exe'
    & cl.exe /nologo /EHs /O2 /std:c++17 /MT /DNOMINMAX `
        "/I$vcpkg/include" "/I$repo/cpp/common" "/I$repo/cpp/common/platform/win" `
        "/Fo$outDir/owned_frame.obj" "/Fe$exe" "$PSScriptRoot/owned_frame.cpp" `
        /link "/LIBPATH:$vcpkg/lib" (Join-Path $native.out_dir 'hwcodec.lib') `
        avcodec.lib avutil.lib avformat.lib libmfx.lib d3d11.lib dxgi.lib `
        user32.lib bcrypt.lib ole32.lib advapi32.lib gdi32.lib shell32.lib oleaut32.lib uuid.lib
    if ($LASTEXITCODE -ne 0) { throw 'Building ownership test failed.' }
    & $exe | Tee-Object -FilePath "$outDir/owned_frame.log"
    if ($LASTEXITCODE -ne 0) { throw 'Ownership test failed.' }
} finally { Pop-Location }
