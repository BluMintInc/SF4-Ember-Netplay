function Get-DownloadSha256([string]$Path) {
    $stream = [IO.File]::OpenRead($Path)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $hash.Dispose(); $stream.Dispose() }
}
function Test-UpdaterDownload {
    param([string]$Label, [string]$Url, [string]$ExpectedSha256,
        [string]$Validator, [scriptblock]$Download)
    $root = Join-Path ([IO.Path]::GetTempPath()) ('ember-download-check-' + [guid]::NewGuid().ToString('N'))
    $result = [ordered]@{ method=$Label; passed=$false; bytes=0; sha256=''; error='' }
    try {
        if ($ExpectedSha256 -notmatch '^[0-9a-fA-F]{64}$') { throw 'Selected asset has no valid expected SHA-256' }
        if (!(Test-Path -LiteralPath $Validator -PathType Leaf)) { throw 'Native package validator is missing' }
        $null = New-Item -ItemType Directory -Path $root
        $archive = Join-Path $root 'download.zip'
        & $Download $Url $archive | Out-Null
        if (!(Test-Path -LiteralPath $archive -PathType Leaf)) { throw 'Download produced no file' }
        $result.bytes = (Get-Item -LiteralPath $archive).Length
        $result.sha256 = Get-DownloadSha256 $archive
        if ($result.sha256 -ne $ExpectedSha256) { throw 'Selected asset SHA-256 mismatch' }
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::OpenRead($archive)
        $extract = Join-Path $root 'extracted'
        $prefix = [IO.Path]::GetFullPath($extract) + [IO.Path]::DirectorySeparatorChar
        $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        try {
            if (!$zip.Entries.Count -or $zip.Entries.Count -gt 20000) { throw 'Invalid archive entry count' }
            $total = [long]0
            foreach ($entry in $zip.Entries) {
                $name = $entry.FullName.Replace('\','/')
                $parts = $name.TrimEnd('/').Split('/')
                if ($name.StartsWith('/') -or $name.Contains(':') -or @($parts | Where-Object { !$_ -or $_ -in '.', '..' -or $_ -match '[. ]$' }).Count) { throw 'Unsafe archive path' }
                if (!([IO.Path]::GetFullPath((Join-Path $extract $name))).StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Archive path escapes extraction root' }
                if (!$names.Add($name.TrimEnd('/'))) { throw 'Duplicate archive path' }
                if ((($entry.ExternalAttributes -shr 16) -band 0xF000) -eq 0xA000) { throw 'Archive contains a symbolic link' }
                $total += $entry.Length
                if ($total -gt 2GB) { throw 'Archive exceeds package extraction limit' }
            }
        } finally { $zip.Dispose() }
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $extract)
        $entries = @(Get-ChildItem -LiteralPath $extract -Force)
        $package = $extract
        if ($entries.Count -eq 1 -and $entries[0].PSIsContainer) { $package = $entries[0].FullName }
        & $Validator --validate $package | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Native package validation failed ($LASTEXITCODE)" }
        $result.passed = $true
    } catch { $result.error = $_.Exception.Message }
    finally {
        # Only this invocation's GUID directory can be removed.
        $resolved = [IO.Path]::GetFullPath($root)
        $temp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\','/') + [IO.Path]::DirectorySeparatorChar
        if (!$resolved.StartsWith($temp, [StringComparison]::OrdinalIgnoreCase) -or
            [IO.Path]::GetFileName($resolved) -notmatch '^ember-download-check-[0-9a-f]{32}$') { throw 'Unsafe temporary cleanup path' }
        if (Test-Path -LiteralPath $resolved) { Remove-Item -LiteralPath $resolved -Recurse -Force }
    }
    [pscustomobject]$result
}
