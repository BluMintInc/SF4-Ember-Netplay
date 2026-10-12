param([Parameter(Mandatory=$true)][string]$Validator, [string]$PackageZip = '')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'UpdaterDownloadValidation.ps1')
Add-Type -AssemblyName System.IO.Compression.FileSystem
$root = Join-Path ([IO.Path]::GetTempPath()) ('ember-download-fixtures-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $root
function Check-Download([string]$file, [string]$hash, [string]$expectedError, [scriptblock]$transport = {param($url,$dest) Copy-Item -LiteralPath $url -Destination $dest}) {
    $result = Test-UpdaterDownload 'fixture' $file $hash $Validator $transport
    if ($expectedError) {
        if ($result.passed -or $result.error -notmatch $expectedError) { throw "Wrong refusal: $($result | ConvertTo-Json -Compress)" }
    } elseif (!$result.passed) { throw "Valid package refused: $($result.error)" }
}
function Make-Zip([string]$name, [string[]]$entries) {
    $path = Join-Path $root $name
    $zip = [IO.Compression.ZipFile]::Open($path, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($entry in $entries) {
            $writer = [IO.StreamWriter]::new($zip.CreateEntry($entry).Open())
            try { $writer.Write('not a valid package') } finally { $writer.Dispose() }
        }
    } finally { $zip.Dispose() }
    return $path
}
try {
    $body = Join-Path $root 'error.html'
    Set-Content -LiteralPath $body -Value '<html>download failed</html>'
    $hash = Get-DownloadSha256 $body
    Check-Download $body $hash 'transport failed' {param($url,$dest) Copy-Item -LiteralPath $url -Destination $dest; throw 'transport failed'}
    Check-Download $body ('0' * 64) 'SHA-256 mismatch'
    Check-Download $body '' 'expected SHA-256'
    Check-Download $body $hash 'Central Directory|archive|zip'
    foreach ($case in @(
        @{name='escape.zip';entries=@('../escape.txt');error='Unsafe archive path'},
        @{name='duplicate.zip';entries=@('x.txt','X.txt');error='Duplicate archive path'},
        @{name='invalid-package.zip';entries=@('Launcher.exe');error='Native package validation failed'})) {
        $file = Make-Zip $case.name $case.entries
        Check-Download $file (Get-DownloadSha256 $file) $case.error
    }
    if ($PackageZip) { Check-Download $PackageZip (Get-DownloadSha256 $PackageZip) '' }
    Write-Output 'Download transport, checksum, archive and native package validation tests passed.'
} finally {
    $resolved = [IO.Path]::GetFullPath($root)
    $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
    if (!$resolved.StartsWith($temp, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^ember-download-fixtures-[0-9a-f]{32}$') { throw 'Unsafe fixture cleanup path' }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
