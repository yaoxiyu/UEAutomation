param(
    [Parameter(Mandatory = $true)]
    [string]$TaskId,

    [string]$ResultDir = "C:\UEAutomation\results",

    [int]$TimeoutSeconds = 300,

    [switch]$FailOnTaskFailure
)

$ErrorActionPreference = "Stop"

$resultPath = Join-Path $ResultDir "$TaskId.result.json"
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)

while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $resultPath) {
        $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
        $result | ConvertTo-Json -Depth 32

        if ($FailOnTaskFailure -and -not $result.success) {
            exit 2
        }
        exit 0
    }

    Start-Sleep -Seconds 1
}

Write-Error "Timed out waiting for result: $resultPath"
exit 1
