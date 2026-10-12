function Test-NetworkCompletion {
    param([string[]]$Lines, [string]$ExpectedCase, [int]$ExitCode, [bool]$ArtifactsUnchanged)
    if ($ExitCode -ne 0 -or !$ArtifactsUnchanged) { return $false }
    # Exactly one canonical v1 record. A second record, unknown fields,
    # duplicate keys, another version or truncated JSON cannot mean success.
    $records = @($Lines | Where-Object { $_.Contains('EMBER_NETWORK_RESULT') })
    if ($records.Count -ne 1) { return $false }
    if ($records[0] -cnotmatch '^EMBER_NETWORK_RESULT \{"version":1,"case":"([a-z-]+)","status":"success"\}$') { return $false }
    return $Matches[1] -ceq $ExpectedCase
}
