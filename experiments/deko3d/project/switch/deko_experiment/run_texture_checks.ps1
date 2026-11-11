param(
 [string]$CMake='C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
)
$ErrorActionPreference='Stop'
$taskWorkspace=(Resolve-Path (Join-Path $PSScriptRoot '../../../../..')).Path
$taskBuild=Join-Path $taskWorkspace 'work/green-lantern-360/project/out/build/win-amd64-vulkan'
$taskChecks=Join-Path $taskWorkspace 'outputs/deko-native/texture-check-bin'
New-Item -ItemType Directory -Force $taskChecks | Out-Null
& $CMake --build $taskBuild --target green_lantern_texture_checks --parallel 1
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
Copy-Item -LiteralPath "$taskBuild/green_lantern_texture_checks.exe" -Destination $taskChecks
# Always use the just-linked runtime, rather than a stale DLL beside the game EXE.
Get-ChildItem -LiteralPath "$taskBuild/sdk-bin" -Filter '*.dll' | Copy-Item -Destination $taskChecks
& "$taskChecks/green_lantern_texture_checks.exe"
exit $LASTEXITCODE
