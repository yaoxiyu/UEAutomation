param(
    [Parameter(Mandatory = $true)]
    [string]$TaskPath,

    [string]$InboxDir = "C:\UEAutomation\tasks\inbox",

    [string]$ResultDir = "C:\UEAutomation\results",

    [int]$TimeoutSeconds = 300,

    [switch]$FailOnTaskFailure
)

$ErrorActionPreference = "Stop"

$task = Get-Content -LiteralPath $TaskPath -Raw | ConvertFrom-Json
if (-not $task.task_id) {
    throw "Task file is missing task_id: $TaskPath"
}

& "$PSScriptRoot\Submit-AutomationTask.ps1" -TaskPath $TaskPath -InboxDir $InboxDir | Out-Null

$resultPath = Join-Path $ResultDir "$($task.task_id).result.json"
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$completed = $false

while ((Get-Date) -lt $deadline) {
    if (Test-Path -LiteralPath $resultPath) {
        $result = Get-Content -LiteralPath $resultPath -Raw | ConvertFrom-Json
        $result | ConvertTo-Json -Depth 32

        if ($FailOnTaskFailure -and -not $result.success) {
            exit 2
        }
        $completed = $true
        break
    }

    Start-Sleep -Seconds 1
}

if (-not $completed) {
    Write-Error "Timed out waiting for result: $resultPath"
    exit 1
}
