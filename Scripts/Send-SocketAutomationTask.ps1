param(
    [Parameter(Mandatory = $true)]
    [string]$TaskPath,

    [string]$HostName = "127.0.0.1",

    [int]$Port = 18777,

    [int]$TimeoutSeconds = 30,

    [switch]$FailOnTaskFailure
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $TaskPath)) {
    throw "Task file not found: $TaskPath"
}

$json = (Get-Content -LiteralPath $TaskPath -Raw | ConvertFrom-Json) | ConvertTo-Json -Depth 32 -Compress
$client = [System.Net.Sockets.TcpClient]::new()
$async = $client.BeginConnect($HostName, $Port, $null, $null)
if (-not $async.AsyncWaitHandle.WaitOne([TimeSpan]::FromSeconds($TimeoutSeconds))) {
    $client.Close()
    throw "Timed out connecting to ${HostName}:$Port"
}
$client.EndConnect($async)

try {
    $stream = $client.GetStream()
    $stream.WriteTimeout = $TimeoutSeconds * 1000
    $stream.ReadTimeout = $TimeoutSeconds * 1000

    $payload = [System.Text.Encoding]::UTF8.GetBytes($json + "`n")
    $stream.Write($payload, 0, $payload.Length)

    $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8)
    $response = $reader.ReadToEnd()
    if (-not $response) {
        throw "Socket server returned an empty response."
    }

    $result = $response | ConvertFrom-Json
    $result | ConvertTo-Json -Depth 32

    if ($FailOnTaskFailure -and -not $result.success) {
        exit 2
    }
}
finally {
    $client.Close()
}
