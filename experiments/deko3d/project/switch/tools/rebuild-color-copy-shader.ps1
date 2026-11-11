param([Parameter(Mandatory=$true)][string]$Compiler)
$ErrorActionPreference = 'Stop'
$nativeRoot = Join-Path $PSScriptRoot '../native_experiment/native/shaders'
& $Compiler -S comp -V --target-env vulkan1.1 --vn kCopyColorCs -o (Join-Path $nativeRoot 'copy_color_cs.h') (Join-Path $nativeRoot 'copy_color.cs.glsl')
if ($LASTEXITCODE -ne 0) { throw 'Falha ao compilar copy_color.cs.glsl' }
