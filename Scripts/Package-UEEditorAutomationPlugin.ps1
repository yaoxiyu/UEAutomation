<#
.SYNOPSIS
Packages UEEditorAutomation with RunUAT BuildPlugin.

.PARAMETER ProjectRoot
Required. Accepts either the Unreal project root that contains the .uproject
and Plugins/UEEditorAutomation, or a source checkout root that contains Engine
and one project directory with Plugins/UEEditorAutomation.

.PARAMETER Configuration
Build configuration. Development uses RunUAT BuildPlugin. DebugGame uses UBT to
compile the project editor target, then stages the plugin directory.

.PARAMETER PackageArgs
Optional BuildPlugin arguments for Development packaging. Defaults to
-TargetPlatforms=Win64.

.EXAMPLE
.\Scripts\Package-UEEditorAutomationPlugin.ps1 -ProjectRoot "D:\YourProject"

.EXAMPLE
.\Scripts\Package-UEEditorAutomationPlugin.ps1 -ProjectRoot "D:\YourProject" -PackageArgs "-TargetPlatforms=Win64","-Rocket"

.EXAMPLE
.\Scripts\Package-UEEditorAutomationPlugin.ps1 -ProjectRoot "D:\YourProject" -Configuration DebugGame
#>

param(
    [string]$ProjectRoot,

    [ValidateSet("Development", "DebugGame")]
    [string]$Configuration = "Development",

    [string]$TargetName,

    [string[]]$PackageArgs = @("-TargetPlatforms=Win64"),

    [switch]$Help
)

$ErrorActionPreference = "Stop"

function Show-Usage {
    Write-Host @'
Usage:
  .\Scripts\Package-UEEditorAutomationPlugin.ps1 -ProjectRoot <ProjectRoot> [-Configuration Development|DebugGame] [-PackageArgs <args[]>]

Required:
  -ProjectRoot   Project root containing Plugins\UEEditorAutomation, or source
                 checkout root containing Engine and the project directory.

Optional:
  -Configuration Build configuration. Default: Development.
                 Development uses RunUAT BuildPlugin.
                 DebugGame uses UBT to build the project editor target, then
                 stages the plugin directory.
  -TargetName    Editor target name for DebugGame. Default: <uproject name>Editor.
  -PackageArgs   Extra arguments passed to RunUAT BuildPlugin.
                 Default: -TargetPlatforms=Win64
  -Help          Show this usage text.

Examples:
  .\Scripts\Package-UEEditorAutomationPlugin.ps1 -ProjectRoot "D:\YourProject"

  .\Scripts\Package-UEEditorAutomationPlugin.ps1 -ProjectRoot "D:\YourSourceCheckout"

  .\Scripts\Package-UEEditorAutomationPlugin.ps1 `
    -ProjectRoot "D:\YourProject" `
    -PackageArgs "-TargetPlatforms=Win64","-Rocket"

  .\Scripts\Package-UEEditorAutomationPlugin.ps1 `
    -ProjectRoot "D:\YourProject" `
    -Configuration DebugGame
'@
}

if ($Help -or [string]::IsNullOrWhiteSpace($ProjectRoot)) {
    Show-Usage
    if ($Help) {
        exit 0
    }
    exit 1
}

$ResolvedInputRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path

if (Test-Path -LiteralPath (Join-Path $ResolvedInputRoot "Plugins\UEEditorAutomation\UEEditorAutomation.uplugin")) {
    $ResolvedProjectRoot = $ResolvedInputRoot
    $EngineRoot = Join-Path (Split-Path $ResolvedProjectRoot -Parent) "Engine"
}
elseif (Test-Path -LiteralPath (Join-Path $ResolvedInputRoot "Engine\Build\BatchFiles\RunUAT.bat")) {
    $EngineRoot = Join-Path $ResolvedInputRoot "Engine"
    $ProjectCandidates = Get-ChildItem -LiteralPath $ResolvedInputRoot -Directory |
        Where-Object {
            Test-Path -LiteralPath (Join-Path $_.FullName "Plugins\UEEditorAutomation\UEEditorAutomation.uplugin")
        }

    if ($ProjectCandidates.Count -eq 0) {
        throw "No project containing Plugins\UEEditorAutomation was found under: $ResolvedInputRoot"
    }
    if ($ProjectCandidates.Count -gt 1) {
        $Names = ($ProjectCandidates | ForEach-Object { $_.FullName }) -join ", "
        throw "Multiple projects containing Plugins\UEEditorAutomation were found. Pass the project root instead. Candidates: $Names"
    }

    $ResolvedProjectRoot = $ProjectCandidates[0].FullName
}
else {
    throw "ProjectRoot must be either a project root with Plugins\UEEditorAutomation or a source checkout root with Engine."
}

$RunUAT = Join-Path $EngineRoot "Build\BatchFiles\RunUAT.bat"
$UBT = Join-Path $EngineRoot "Binaries\DotNET\UnrealBuildTool.exe"
$PluginFile = Join-Path $ResolvedProjectRoot "Plugins\UEEditorAutomation\UEEditorAutomation.uplugin"
$PluginRoot = Split-Path $PluginFile -Parent
$PackageDir = Join-Path $ResolvedProjectRoot "PluginPackages\UEEditorAutomation_UE4.25_Win64_$Configuration"

if (-not (Test-Path -LiteralPath $RunUAT)) {
    throw "RunUAT.bat was not found: $RunUAT"
}

if (-not (Test-Path -LiteralPath $PluginFile)) {
    throw "UEEditorAutomation.uplugin was not found: $PluginFile"
}

if ($Configuration -eq "Development") {
    $Arguments = @(
        "BuildPlugin",
        "-Plugin=$PluginFile",
        "-Package=$PackageDir"
    ) + $PackageArgs

    Write-Host "RunUAT: $RunUAT"
    Write-Host "Plugin: $PluginFile"
    Write-Host "Package: $PackageDir"
    Write-Host "Configuration: $Configuration"
    Write-Host "Args: $($PackageArgs -join ' ')"

    & $RunUAT @Arguments
    exit $LASTEXITCODE
}

if (-not (Test-Path -LiteralPath $UBT)) {
    throw "UnrealBuildTool.exe was not found: $UBT"
}

$ProjectFiles = @(Get-ChildItem -LiteralPath $ResolvedProjectRoot -Filter *.uproject -File)
if ($ProjectFiles.Count -eq 0) {
    throw "No .uproject file was found under: $ResolvedProjectRoot"
}
if ($ProjectFiles.Count -gt 1) {
    $Names = ($ProjectFiles | ForEach-Object { $_.FullName }) -join ", "
    throw "Multiple .uproject files were found. Keep one project per root or extend this script. Candidates: $Names"
}

$UProjectFile = $ProjectFiles[0].FullName
if ([string]::IsNullOrWhiteSpace($TargetName)) {
    $TargetName = "$($ProjectFiles[0].BaseName)Editor"
}

$BuildArguments = @(
    "-Target=$TargetName Win64 $Configuration",
    "-Project=$UProjectFile",
    "-plugin=$PluginFile",
    "-nohotreload"
)

Write-Host "UBT: $UBT"
Write-Host "Project: $UProjectFile"
Write-Host "Plugin: $PluginFile"
Write-Host "Package: $PackageDir"
Write-Host "Configuration: $Configuration"
Write-Host "Target: $TargetName"

& $UBT @BuildArguments
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$PackageRoot = Join-Path $ResolvedProjectRoot "PluginPackages"
if (-not (Test-Path -LiteralPath $PackageRoot)) {
    New-Item -ItemType Directory -Path $PackageRoot | Out-Null
}
if (Test-Path -LiteralPath $PackageDir) {
    $ResolvedPackageDir = (Resolve-Path -LiteralPath $PackageDir).Path
    $ResolvedPackageRoot = (Resolve-Path -LiteralPath $PackageRoot).Path
    if (-not $ResolvedPackageDir.StartsWith($ResolvedPackageRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove package directory outside PluginPackages: $ResolvedPackageDir"
    }
    Remove-Item -LiteralPath $PackageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $PackageDir | Out-Null

$Items = @(
    "UEEditorAutomation.uplugin",
    "README.md",
    "AGENTS.md",
    "Config",
    "Docs",
    "Scripts",
    "Samples",
    "Source",
    "Binaries",
    ".agents"
)

foreach ($Item in $Items) {
    $Source = Join-Path $PluginRoot $Item
    if (Test-Path -LiteralPath $Source) {
        Copy-Item -LiteralPath $Source -Destination (Join-Path $PackageDir $Item) -Recurse -Force
    }
}

Write-Host "Packaged DebugGame plugin to: $PackageDir"
exit 0
