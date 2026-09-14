# Stage the redistributable CRT from the selected Visual Studio installation.
# The application and xmake dependencies use /MD; never rely on runner System32.
param([string]$Output = 'build/windows/x64/release')
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio C++ installation not found' }
$version = (Get-Content (Join-Path $vs 'VC/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt') -Raw).Trim()
$crt = Join-Path $vs "VC/Redist/MSVC/$version/x64/Microsoft.VC143.CRT"
foreach ($required in @('msvcp140.dll', 'vcruntime140.dll')) {
    if (!(Test-Path (Join-Path $crt $required))) { throw "Missing redistributable $required" }
}
$records = @()
foreach ($dll in Get-ChildItem $crt -Filter '*.dll') {
    Copy-Item $dll.FullName $Output -Force
    $records += @{ name = $dll.Name; sha256 = (Get-FileHash $dll.FullName -Algorithm SHA256).Hash.ToLower() }
}
@{ source = 'Visual Studio 2022 x64 redistributable CRT'; version = $version; files = $records } |
    ConvertTo-Json -Depth 5 | Set-Content -Encoding utf8 (Join-Path $Output 'native-dependencies.json')
