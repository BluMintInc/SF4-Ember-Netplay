# Read-only release download diagnostic. No installer or game is started.
param([string]$Repo = 'Confetti3/SF4-Ember-Netplay', [string]$Validator = '', [string]$Receipt = '')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildEnvironment.ps1')
. (Join-Path $PSScriptRoot 'UpdaterDownloadValidation.ps1')
$source = Split-Path $PSScriptRoot -Parent
if (!$Validator) {
    $target = Get-EmberBuildTarget $source
    $Validator = Join-Path (Join-Path $source $target.buildDirectory) 'PackageInstallerTest.exe'
}
$release = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -Headers @{
    Accept='application/vnd.github+json'; 'User-Agent'='sf4e-updater-test'
}
$asset = $release.assets | Where-Object { $_.name -clike 'sf4-ember-netplay-*.zip' } | Select-Object -First 1
if (!$asset -or $asset.digest -cnotmatch '^sha256:([0-9a-fA-F]{64})$') { throw 'Selected package asset requires an expected SHA-256 digest' }
$expected = $Matches[1]
$results = @(
    Test-UpdaterDownload 'browser-iwr' $asset.browser_download_url $expected $Validator {
        param($Url,$Dest)
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $Url -OutFile $Dest -UseBasicParsing -MaximumRedirection 10
    }
    Test-UpdaterDownload 'curl' $asset.browser_download_url $expected $Validator {
        param($Url,$Dest)
        & curl.exe -fL --retry 2 --connect-timeout 30 --max-time 300 -o $Dest $Url
        if ($LASTEXITCODE -ne 0) { throw "curl transport failed ($LASTEXITCODE)" }
    }
    Test-UpdaterDownload 'api-iwr' $asset.url $expected $Validator {
        param($Url,$Dest)
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        Invoke-WebRequest -Uri $Url -OutFile $Dest -Headers @{Accept='application/octet-stream'} -UseBasicParsing -MaximumRedirection 10
    }
)
$report = [ordered]@{release=$release.tag_name; asset=$asset.name; expectedSha256=$expected; methods=$results}
if ($Receipt) { $report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $Receipt }
$results | Format-Table method,passed,bytes,error -AutoSize
if (@($results | Where-Object passed).Count) { exit 0 }
exit 1
