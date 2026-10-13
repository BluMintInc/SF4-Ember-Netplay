param([Parameter(Mandatory)][string]$Emitter)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'NetworkCompletion.ps1')
# The emitter prints the record through the same header the network fixtures use,
# so the validator is tested against the bytes a fixture really writes.
function Get-EmittedRecord([string]$name) { & $Emitter "--network-case=$name" }
foreach ($name in 'room','terminal-recovery','four-tables') {
    if (!(Test-NetworkCompletion (@('arbitrary human wording') + (Get-EmittedRecord $name)) $name 0 $true)) { throw "emitted $name record refused" }
}
if (@(& $Emitter).Count) { throw 'a run without --network-case emitted a record' }
foreach ($bad in 'Room','a_b','') {
    & $Emitter "--network-case=$bad"
    if ($LASTEXITCODE -ne 2) { throw "emitter accepted --network-case=$bad" }
}
$good = [string](Get-EmittedRecord 'room')
foreach ($lines in @(@(), @('six games passed'), @($good, $good), @($good.Replace('1,','2,')),
    @($good.Replace('success','failed')), @($good.Replace('room','rejoin')), @($good + ' garbage'),
    @($good.Replace('"version":1', '"version":1,"version":1')), @($good.Substring(0, 40)))) {
    if (Test-NetworkCompletion $lines 'room' 0 $true) { throw "invalid completion accepted: $lines" }
}
if (Test-NetworkCompletion @($good) 'room' 1 $true) { throw 'nonzero exit accepted' }
if (Test-NetworkCompletion @($good) 'room' 0 $false) { throw 'changed artifacts accepted' }
Write-Output 'Structured network completion tests passed.'
