# Estado atual — 15/10/2026, execução 052

## Resumo

O jogo mostra imagem e entra no início jogável no Switch. Persistem personagens escuros,
iluminação/composição incorretas e desempenho baixo em certas cenas. A versão de PC usa
Xenos/D3D12; o Switch usa o renderer nativo adaptado de Simpsons. Não são backends idênticos.

## Fatos confirmados

- 052 executou a conversão de formato e expoente dos resolves: `[gl-resolve]` registra seu uso.
- Pico amostrado do malloc: 1372,9 MiB; pools do guest: 424 MiB.
- Zero falhas nas categorias Vulkan monitoradas; zero overflow do rastreador.
- Heap configurada em 2000 MiB, arena 448 MiB. Malloc inclui backing e alocações do driver:
  não somar essas categorias novamente. `malloc_free` é agregado, não maior bloco contíguo.
- Perfil do 052 registra intervalos de 28,3 FPS, depois 8,2 e 8,9 FPS; não atribuir uma taxa
  única à sessão. Capturas de diagnóstico também acrescentam espera e não medem FPS normal.
- Crash anexado ao 052 contém registros anteriores (incluindo base 165823D000 do 049).
  Stderr/perfil atuais têm base 4D284DF000. Não interpretar arquivo acumulado como novo crash.
- Nas fotos e quadros amostrados do vídeo, interface e cenário aparecem, mas o personagem
  do menu tem grandes áreas escuras. A câmera/exposição impedem medir cores numericamente.

## Próximo passo preparado

Captura limitada de um quadro por L+R+−, até quatro pedidos por sessão.
Cada quadro salva até 64 imagens / 64 MiB de readback, em formato bruto + JSON de layout.
Inclui origem de resolves de cor sem MSAA antes da conversão, destinos e log de draws/resolve.
Não é replay completo do fluxo Xenos, não captura todos os buffers, nem todos os depth targets.
Backend e orçamento normais foram mantidos; não é candidato que prometa corrigir os gráficos.
Build/link concluídos, símbolo do atalho presente no ELF; validação no hardware pendente.

## Hipóteses a distinguir com as capturas

1. Origem correta e destino incorreto: revisar conversão, escala, swizzle e limites do resolve.
2. Origem já incorreta: revisar shaders, vértices, textura amostrada, profundidade e blend do passe.
3. Conteúdo correto isolado mas errado quando reutilizado: revisar alias EDRAM, pitch/dimensões
   e invalidação do cache de destinos. A relação 1280x360/640x360 foi observada, não provada causa.
4. Ausência de recurso ou erro de alocação durante captura: não usar a imagem incompleta
   para concluir sobre a renderização normal.

## Limites e caminhos que não devem ser repetidos sem evidência nova

- Aumentar a heap não corrige automaticamente shaders ou composição de passes.
- O problema antigo de 1,5 GiB com Xenos não estabelece um limite para o renderer nativo.
- Crescimento de render target já preserva o conteúdo; não aplicar correção que presuma descarte.
- Não reativar vigilância por faults no modo readonly_mapped do renderer nativo.
- Chamadas experimentais de SHM/mapas e aumento cego de pools não são o próximo passo.
- O diagnóstico novo ainda não foi comparado com uma captura PC: a causa visual exata segue aberta.
