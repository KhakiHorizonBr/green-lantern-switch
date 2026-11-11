# Reaplica o diagnóstico após rexglue codegen, sem alterar o comportamento guest.
$generated = Join-Path $PSScriptRoot '../../generated/hinted'
$path = Join-Path $generated 'green_lantern_recomp.84.cpp'
$text = [IO.File]::ReadAllText($path)
if (!$text.Contains('GL_RELOCATION_DIAGNOSTIC')) {
  $start = $text.IndexOf('DEFINE_REX_FUNC(sub_8219F438) {')
  $end = $text.IndexOf('DEFINE_REX_FUNC(', $start + 20)
  if ($start -lt 0 -or $end -lt 0) { throw 'Função de relocação não encontrada' }
  $fn = $text.Substring($start, $end - $start)
  $needle = "`tREX_STORE_U32(ctx.r7.u32 + ctx.r4.u32, ctx.r6.u32);"
  if (!$fn.Contains($needle)) { throw 'Store esperado não encontrado' }
  $diagnostic = @"
#if defined(__SWITCH__)
    // GL_RELOCATION_DIAGNOSTIC: registra a origem, sem ocultar a falha.
    if (ctx.r7.u32 + ctx.r4.u32 == 0) {
      std::fprintf(stderr, "[gl-reloc] destino nulo: section=%08X base=%08X entry=%08X group=%u item=%u offset=%08X value=%08X lr=%08X\n",
          ctx.r29.u32, ctx.r28.u32, ctx.r8.u32, ctx.r26.u32,
          ctx.r9.u32, ctx.r7.u32, ctx.r6.u32, uint32_t(ctx.lr));
      for (uint32_t word = 0; word < 18; ++word)
        std::fprintf(stderr, "[gl-reloc] section+%02X=%08X\n", word * 4,
                     REX_LOAD_U32(ctx.r29.u32 + word * 4));
      for (uint32_t word = 0; word < 5; ++word)
        std::fprintf(stderr, "[gl-reloc] entry+%02X=%08X\n", word * 4,
                     REX_LOAD_U32(ctx.r8.u32 + word * 4));
      std::fflush(stderr);
    }
#endif
$needle
"@
  $fn = $fn.Replace($needle, $diagnostic)
  $text = $text.Substring(0,$start) + $fn + $text.Substring($end)
  $text = "#include <cstdio>`n" + $text
  [IO.File]::WriteAllText($path, $text)
}
