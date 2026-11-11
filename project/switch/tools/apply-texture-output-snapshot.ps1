# Preserva os ponteiros de saída conforme as atribuições iniciais da rotina.
$path=Join-Path $PSScriptRoot '../../generated/hinted/green_lantern_recomp.93.cpp'
$text=[IO.File]::ReadAllText($path)
if (!$text.Contains('GL_TEXTURE_OUTPUT_SNAPSHOT')) {
  $start=$text.IndexOf('DEFINE_REX_FUNC(sub_825AA8B0) {')
  $end=$text.IndexOf('DEFINE_REX_FUNC(', $start+20)
  if ($start -lt 0 -or $end -lt 0) { throw 'Rotina não encontrada' }
  $fn=$text.Substring($start,$end-$start)
  $entry=@'
DEFINE_REX_FUNC(sub_825AA8B0) {
#if defined(__SWITCH__)
    // GL_TEXTURE_OUTPUT_SNAPSHOT: destinos invariantes nessa função.
    const uint32_t gl_texture_outputs[] = {
        ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32, ctx.r9.u32};
#endif
'@
  $fn=$fn.Replace('DEFINE_REX_FUNC(sub_825AA8B0) {', $entry)
  $pairs=@(@('r20','r25'),@('r19','r27'),@('r18','r24'),@('r17','r31'),@('r16','r11'))
  for ($i=0; $i -lt $pairs.Count; ++$i) {
    $reg=$pairs[$i][0]; $value=$pairs[$i][1]
    $needle="REX_STORE_U32(ctx.$reg.u32 + 0, ctx.$value.u32);"
    if (!$fn.Contains($needle)) { throw "Store ausente: $reg" }
    $replacement=@"
#if defined(__SWITCH__)
    if (ctx.$reg.u32 != gl_texture_outputs[$i]) {
      std::fprintf(stderr, "[gl-texture-output] slot=$i captured=%08X current=%08X value=%08X\n",
                   gl_texture_outputs[$i], ctx.$reg.u32, ctx.$value.u32);
      std::fflush(stderr);
    }
    REX_STORE_U32(gl_texture_outputs[$i], ctx.$value.u32);
#else
    $needle
#endif
"@
    $fn=$fn.Replace($needle,$replacement)
  }
  $text=$text.Substring(0,$start)+$fn+$text.Substring($end)
  $text="#include <cstdio>`n"+$text
  [IO.File]::WriteAllText($path,$text)
}
