# Reaplica snapshots após codegen. Não modifica registradores ou fluxo guest.
$generated = Join-Path $PSScriptRoot '../../generated/hinted'
foreach ($partition in @(35,93)) {
  $path = Join-Path $generated "green_lantern_recomp.$partition.cpp"
  $text = [IO.File]::ReadAllText($path)
  if (!$text.Contains('vector_diagnostic.h')) {
    $text = '#include "../../switch/runtime/vector_diagnostic.h"' + "`n" + $text
  }
  if ($partition -eq 35 -and !$text.Contains('GL_VECTOR_INIT')) {
    $needle = 'REX_STORE_U32(ctx.r10.u32 + -440, ctx.r11.u32);'
    if (!$text.Contains($needle)) { throw 'Inicializador não encontrado' }
    $text = $text.Replace($needle, $needle + "`n    // GL_VECTOR_INIT`n    GreenLanternVectorSnapshot(ctx, base, 0);")
  }
  if ($partition -eq 93 -and !$text.Contains('GL_VECTOR_ENTRY')) {
    $needle = 'DEFINE_REX_FUNC(sub_82731878) {'
    if (!$text.Contains($needle)) { throw 'Rotina vetorial não encontrada' }
    $text = $text.Replace($needle, $needle + "`n    // GL_VECTOR_ENTRY`n    GreenLanternVectorSnapshot(ctx, base, 1);")
    $needle = 'loc_82731928:'
    if (!$text.Contains($needle)) { throw 'Loop vetorial não encontrado' }
    $text = $text.Replace($needle, $needle + "`n    GreenLanternVectorSnapshot(ctx, base, 2);")
  }
  [IO.File]::WriteAllText($path, $text)
}
