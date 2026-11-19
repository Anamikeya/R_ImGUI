<#
.SYNOPSIS
Creates a new application with R_ImGUI's look, from one of templates/.

.DESCRIPTION
Copies a template and the rimgui libraries into a new folder, and renames the template's program to the name given.
The new folder stands on its own: it builds without this repository.
  -Template starter     a tool with the editor's look (the default)
  -Template renderlab   a realtime rendering research application: techniques, hot-reloaded shaders, A/B
                        comparison, GPU timings, a texture inspector

.EXAMPLE
.\tools\new_app.ps1 -Name AssetViewer -Destination D:\Projects
Creates D:\Projects\AssetViewer, with AssetViewer.exe as its program.

.EXAMPLE
.\tools\new_app.ps1 -Name ShadowLab -Template renderlab -Destination D:\Research -Build
A renderer research application, ShadowLab.exe, configured and built.
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]{0,63}$')]
    [string]$Name,
    [string]$Destination = (Get-Location).Path,
    [ValidateSet('starter', 'renderlab')]
    [string]$Template = 'starter',
    [switch]$Build
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repo "templates\$Template"
$original = if ($Template -eq 'renderlab') { 'RenderLab' } else { 'StarterApp' }
$target = Join-Path $Destination $Name
if (Test-Path $target) {
    throw "$target already exists. Choose another name or folder."
}

New-Item -ItemType Directory -Path $target | Out-Null
Copy-Item -Recurse (Join-Path $source '*') $target
Copy-Item -Recurse (Join-Path $repo 'rimgui') (Join-Path $target 'rimgui')
Copy-Item (Join-Path $repo '.gitignore') $target

# The template's name becomes the new one in the build file and the program.
$utf8 = New-Object System.Text.UTF8Encoding $false
foreach ($file in @('CMakeLists.txt', 'src\main.cpp')) {
    $path = Join-Path $target $file
    $text = [IO.File]::ReadAllText($path)
    # The first comment line describes the template itself; the copy gets one of its own.
    $text = $text -replace "^# ${original}: [^\r\n]*", "# ${Name}: made from R_ImGUI's $Template template (https://github.com/Anamikeya/R_ImGUI)."
    $text = $text -replace '^// StarterApp: what a new R_ImGUI application starts as\.', "// ${Name}: made from R_ImGUI's template."
    $text = $text.Replace($original, $Name)
    [IO.File]::WriteAllText($path, $text, $utf8)
}
$readme = @"
# $Name

Made from [R_ImGUI](https://github.com/Anamikeya/R_ImGUI)'s $Template template.

``````
cmake -B build
cmake --build build --config Release
``````

The program is ``build\Release\$Name.exe`` (or ``build\$Name.exe`` with a single-configuration generator).
Its code is in ``src\main.cpp``; the look and the research kit are in ``rimgui\``.
"@
[IO.File]::WriteAllText((Join-Path $target 'README.md'), $readme, $utf8)

Write-Host "Created $target"
if ($Build) {
    Push-Location $target
    try {
        cmake -B build
        if ($LASTEXITCODE -ne 0) { throw 'Configuring failed.' }
        cmake --build build --config Release
        if ($LASTEXITCODE -ne 0) { throw 'Building failed.' }
    } finally {
        Pop-Location
    }
}
