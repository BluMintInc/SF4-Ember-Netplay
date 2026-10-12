$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'NetworkCompletion.ps1')
$good = 'EMBER_NETWORK_RESULT {"version":1,"case":"room","status":"success"}'
if (!(Test-NetworkCompletion @('arbitrary human wording', $good) 'room' 0 $true)) { throw 'valid completion refused' }
foreach ($lines in @(@(), @('six games passed'), @($good, $good), @($good.Replace('1,','2,')),
    @($good.Replace('success','failed')), @($good.Replace('room','rejoin')), @($good + ' garbage'),
    @($good.Replace('"version":1', '"version":1,"version":1')), @($good.Substring(0, 40)))) {
    if (Test-NetworkCompletion $lines 'room' 0 $true) { throw "invalid completion accepted: $lines" }
}
if (Test-NetworkCompletion @($good) 'room' 1 $true) { throw 'nonzero exit accepted' }
if (Test-NetworkCompletion @($good) 'room' 0 $false) { throw 'changed artifacts accepted' }
Write-Output 'Structured network completion tests passed.'
