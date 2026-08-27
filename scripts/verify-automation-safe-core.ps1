[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string]$DllPath
)

$ErrorActionPreference = 'Stop'

$resolvedDll = (Resolve-Path -LiteralPath $DllPath).Path
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio locator not found: $vswhere"
}

$visualStudioRoot = & $vswhere -latest -products * -property installationPath
$dumpbin = Get-ChildItem -LiteralPath (Join-Path $visualStudioRoot 'VC\Tools\MSVC') `
    -Filter dumpbin.exe -Recurse |
    Where-Object FullName -Match '\\Hostx64\\x64\\dumpbin\.exe$' |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $dumpbin) {
    throw '64-bit dumpbin.exe was not found.'
}

$exports = (& $dumpbin /nologo /exports $resolvedDll) -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "dumpbin failed for $resolvedDll"
}

$requiredExports = @('StartSKSE', 'SKSECore_Version')
$missingExports = $requiredExports | Where-Object { $exports -notmatch "(?m)\b$([regex]::Escape($_))\b" }

$binaryText = [System.Text.Encoding]::ASCII.GetString([System.IO.File]::ReadAllBytes($resolvedDll))
$requiredMarkers = @(
    'SKSE_AUTOMATION_SILENT_UI',
    'SUPPRESSED PLUGIN UI',
    'automation silent UI: redirected'
)
$missingMarkers = $requiredMarkers | Where-Object { -not $binaryText.Contains($_) }

if ($missingExports.Count -or $missingMarkers.Count) {
    $problems = @()
    if ($missingExports.Count) {
        $problems += "missing exports: $($missingExports -join ', ')"
    }
    if ($missingMarkers.Count) {
        $problems += "missing silent-UI markers: $($missingMarkers -join ', ')"
    }
    throw "Core verification failed ($($problems -join '; '))."
}

$file = Get-Item -LiteralPath $resolvedDll
$hash = Get-FileHash -LiteralPath $resolvedDll -Algorithm SHA256
[pscustomobject]@{
    Path = $resolvedDll
    Length = $file.Length
    SHA256 = $hash.Hash
    RequiredExports = $requiredExports -join ', '
    SilentUiMarkers = $requiredMarkers -join ', '
    Verified = $true
}
