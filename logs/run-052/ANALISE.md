# Execução 052 e comparação gráfica

Logs preservados antes de qualquer limpeza do Temp. Imagens e 11 amostras do vídeo local
consultadas para inspeção visual. O menu mantém áreas grandes do personagem escuras;
o cenário e a interface aparecem. Fotografias não permitem medir cores exatas.

Pico amostrado malloc: 1372,9 MiB; pools do guest 424 MiB. Zero falhas nas categorias
monitoradas e zero overflow. O perfil registra intervalos de 28,3, 8,2 e 8,9 FPS;
não atribuir um FPS único à sessão. Crash acumulado ainda inclui o 049 (base165823D000),
enquanto esta sessão usa base4D284DF000. Não foi identificada nova exceção nesse arquivo.

O log confirma as conversões: VK97→64 (float16 para packed10) e VK78→70 (SNORM16x2
para UNORM16, expoente efetivo15). Correção em uso, participação no defeito ainda não isolada.

## Novo candidato de captura

`green_lantern_native_renderer_graphics_capture.nro`, 54.433.989 bytes.
SHA256: 5AC1CCE41671CE25F4840C01037D0E83468282389A50B81D572216C4C44CC726.
ELF preservado. Build/link concluídos e símbolo do hook confirmado. Não executado no Switch.

L+R+− pede captura do próximo quadro. Até quatro pedidos por sessão; até64 imagens/64MiB
de readback por quadro. Salva origem de resolves de cor sem MSAA e destinos como bytes
originais + JSON de layout, com metadados de draws/resolve no log. Dados sincronizados
após submissão e espera de GPU; memória mapeada invalidada antes da leitura.
Sem mudança de heap, arena, shaders de produção ou número normal de threads.

Captura antiga interpretava alguns formatos HDR/SNORM como RGBA8. Novo decoder Python
produz previews por formato e estatísticas amostradas; raw preserva valores HDR.
Não captura todo o fluxo de comandos nem permite replay integral do Switch.

PC funcional existente: project/out/build/win-amd64-release/green_lantern_next.exe,
Xenos/D3D12. RenderDoc não foi encontrado em Program Files nesta inspeção.
Ainda não foi coletada captura PC. Comparar mesma cena/menu, recursos e função dos passes,
não o índice absoluto do draw ou do frame entre renderers diferentes.

Se origem correta e destino incorreto: conversão/limites; se origem incorreta: draw/shader/
textura/profundidade; se imagens isoladas corretas mas composição errada: blend/alias/cache.
É uma etapa para localizar a causa, não uma correção gráfica já validada.

