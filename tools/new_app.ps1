<#
.SYNOPSIS
Creates a new application with R_ImGUI's look, from template/.

.DESCRIPTION
Copies template/ and the rimgui library into a new folder, and renames StarterApp to the name given. The new folder
stands on its own: it builds without this repository.

.EXAMPLE
.\tools\new_app.ps1 -Name AssetViewer -Destination D:\Projects
Creates D:\Projects\AssetViewer, with AssetViewer.exe as its program.

.EXAMPLE
.\tools\new_app.ps1 -Name AssetViewer -Destination D:\Projects -Build
Also configures and builds it.
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[A-Za-z][A-Za-z0-9_]{0,63}$')]
    [string]$Name,
    [string]$Destination = (Get-Location).Path,
    [switch]$Build
)
$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot
$target = Join-Path $Destination $Name
if (Test-Path $target) {
    throw "$target already exists. Choose another name or folder."
}

New-Item -ItemType Directory -Path $target | Out-Null
Copy-Item -Recurse (Join-Path $repo 'template\*') $target
Copy-Item -Recurse (Join-Path $repo 'rimgui') (Join-Path $target 'rimgui')
Copy-Item (Join-Path $repo '.gitignore') $target

# StarterApp becomes the new name in the build file and the program.
$utf8 = New-Object System.Text.UTF8Encoding $false
foreach ($file in @('CMakeLists.txt', 'src\main.cpp')) {
    $path = Join-Path $target $file
    $text = [IO.File]::ReadAllText($path)
    # The first comment line describes the template itself; the copy gets one of its own.
    $text = $text -replace '^# StarterApp: .*', "# ${Name}: made from R_ImGUI's template (https://github.com/Anamikeya/R_ImGUI)."
    $text = $text -replace '^// StarterApp: what a new R_ImGUI application starts as\.', "// ${Name}: made from R_ImGUI's template."
    $text = $text.Replace('StarterApp', $Name)
    [IO.File]::WriteAllText($path, $text, $utf8)
}
$readme = @"
# $Name

Made from [R_ImGUI](https://github.com/Anamikeya/R_ImGUI)'s template.

``````
cmake -B build
cmake --build build --config Release
``````

The program is ``build\Release\$Name.exe`` (or ``build\$Name.exe`` with a single-configuration generator).
Its panels are in ``src\main.cpp``; the look is in ``rimgui\``.
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
