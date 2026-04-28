param(
    [Parameter(Mandatory = $true)]
    [string]$TaskPath,

    [string]$InboxDir = "C:\UEAutomation\tasks\inbox"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $TaskPath)) {
    throw "Task file not found: $TaskPath"
}

$json = Get-Content -LiteralPath $TaskPath -Raw | ConvertFrom-Json
if (-not $json.task_id) {
    throw "Task file is missing task_id: $TaskPath"
}

New-Item -ItemType Directory -Force -Path $InboxDir | Out-Null

$destination = Join-Path $InboxDir (Split-Path -Leaf $TaskPath)
Copy-Item -LiteralPath $TaskPath -Destination $destination -Force

[PSCustomObject]@{
    task_id = $json.task_id
    task_type = $json.task_type
    submitted_path = $destination
}
