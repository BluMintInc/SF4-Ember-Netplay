# Explicit public-network acceptance for the helper/transport, never SF4.
param(
    [switch]$IncludeLargeRooms,
    [ValidateSet('room','rejoin','game','authorized','recovery','queue-acks','terminal-recovery','four-tables','spectators')]
    [string[]]$Cases = @(),
    [ValidateSet('default','relay')][ValidateCount(1,2)][string[]]$Routes = @('default','relay'),
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'BuildEnvironment.ps1')
. (Join-Path $PSScriptRoot 'NetworkCompletion.ps1')
. (Join-Path $PSScriptRoot 'BuildProvenance.ps1')
$target = Get-EmberBuildTarget $repo
$build = Join-Path $repo $target.buildDirectory
$helper = Join-Path $build 'sf4-net.exe'
if (!$OutputDirectory) {
    $OutputDirectory = Join-Path $build ('network-tests-' + (Get-Date).ToString('yyyyMMdd-HHmmss-fff', [cultureinfo]::InvariantCulture))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw "Run directory already exists: $OutputDirectory" }
$OutputDirectory = (New-Item -ItemType Directory -Path $OutputDirectory).FullName
$plan = @(
    @{name='room';executable='IrohRoomIntegrationTest';arguments=@()},
    @{name='rejoin';executable='IrohRoomIntegrationTest';arguments=@('--rejoin')},
    @{name='game';executable='IrohGameIntegrationTest';arguments=@()},
    @{name='authorized';executable='IrohAuthorizedMatchTest';arguments=@()},
    @{name='recovery';executable='IrohRecoveryIntegrationTest';arguments=@()},
    @{name='queue-acks';executable='IrohRoomIntegrationTest';arguments=@('--queue-acks')},
    @{name='terminal-recovery';executable='IrohAuthorizedMatchTest';arguments=@('--terminal-recovery')}
)
if ($IncludeLargeRooms -or @($Cases | Where-Object { $_ -in 'four-tables','spectators' }).Count) {
    $plan += @{name='four-tables';executable='CustomRoomGameTest';arguments=@()}
    $plan += @{name='spectators';executable='CustomRoomGameTest';arguments=@('--single-table')}
}
if ($Cases.Count) { $plan = @($plan | Where-Object { $_.name -in $Cases }) }
$receipt = [ordered]@{sourceRoot=$repo;branch=(& git -C $repo branch --show-current);
    commit=(& git -C $repo rev-parse HEAD);sourceFingerprint=(Get-SourceFingerprint $repo);
    platform='windows';startedUtc=[DateTime]::UtcNow.ToString('o');components=@();
    evidence='Synthetic public-network transport and GGPO fixtures; no game launch or live gameplay acceptance'}
foreach ($case in $plan) {
    foreach ($route in ($Routes | Select-Object -Unique)) {
        $receipt.components += [pscustomobject]@{test=$case.name;routePolicy=$route;status='unexecuted'}
    }
}
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'network-provenance.json')
$artifacts = @($helper) + @($plan | ForEach-Object { Join-Path $build ($_.executable + '.exe') } | Select-Object -Unique)
$artifacts += @(Get-ChildItem -LiteralPath $build -File -Filter '*.dll' | ForEach-Object { $_.FullName })
$hashes = foreach ($artifact in $artifacts) {
    [ordered]@{path=$artifact;sha256=(Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash}
}
$hashes | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'artifacts.json')
$results = @()
foreach ($case in $plan) {
    foreach ($route in ($Routes | Select-Object -Unique)) {
        $arguments = @($helper) + $case.arguments + "--network-case=$($case.name)"
        if ($route -eq 'relay') { $arguments += '--relay-only' }
        $log = Join-Path $OutputDirectory ($case.name + '-' + $route + '.log')
        $started = [DateTime]::UtcNow
        Write-Output "RUN $($case.name) ($route). Log: $log"
        # Windows PowerShell wraps redirected native stderr as ErrorRecords.
        # Stop would unwind the pipeline on the first diagnostic, before the
        # fixture can finish its failure dump or we can record its exit code.
        # Keep the native process running to completion, then enforce failure
        # using its exit status and the artifact checks below.
        $nativeErrorPreference = $ErrorActionPreference
        try {
            $ErrorActionPreference = 'Continue'
            & (Join-Path $build ($case.executable + '.exe')) @arguments *> $log
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $nativeErrorPreference
        }
        $changed = @($hashes | Where-Object { (Get-FileHash -LiteralPath $_.path -Algorithm SHA256).Hash -ne $_.sha256 })
        $completed=Test-NetworkCompletion (Get-Content -LiteralPath $log) $case.name $exitCode ($changed.Count -eq 0)
        $result = [pscustomobject]@{test=$case.name;executable=$case.executable;routePolicy=$route;
            exitCode=$exitCode;completed=[bool]$completed;artifactsUnchanged=($changed.Count -eq 0);startedUtc=$started.ToString('o');
            elapsedSeconds=([DateTime]::UtcNow-$started).TotalSeconds;log=$log}
        $results += $result
        ($receipt.components | Where-Object { $_.test -eq $case.name -and $_.routePolicy -eq $route }).status =
            $(if ($completed) {'passed'} else {'failed'})
        $receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'network-provenance.json')
        $results | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'results.json')
        if ($changed.Count) { throw "Binaries changed during acceptance; see $OutputDirectory" }
        if ($exitCode -or !$completed) { throw "Failed $($case.name) ($route); see $log" }
        Write-Output "PASS $($case.name) ($route). Synthetic transport/GGPO only. Log: $log"
    }
}
if ($receipt.sourceFingerprint -ne (Get-SourceFingerprint $repo)) { throw 'Source changed during network acceptance; receipt is not valid for the current source.' }
$receipt.completedUtc = [DateTime]::UtcNow.ToString('o')
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'network-provenance.json')
