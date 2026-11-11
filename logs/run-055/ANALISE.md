# Execução 055 — resultado das hipóteses gráficas

## Resultado observado

O usuário relata que o PC Vulkan continua com texturas pretas após permitir
descritores inválidos; no Switch, menu e jogo continuam com os defeitos anteriores.
Não tratar o bypass de descritores como correção. Ele permanece desligado no Switch.

O log do PC foi preservado em pc-vulkan-permitir-descritores.log. Tem inicialização
Vulkan e encerramento normal pela janela, sem avisos de fetch inválido nesta amostra.
Isso é consistente com o bypass ter sido aplicado, mas não prova correção dos materiais.

## O que o Switch confirmou

- Montagem de atlas habilitada no boot.
- [gl-atlas] registra 1B130000, 2048x3072, pitch2048, VK70 e três fragmentos.
  Existem 16 mensagens iniciais limitadas pelo logger; isso não é o total de montagens.
  O resultado visual não melhorou segundo o usuário. A montagem incompleta pode
  existir, mas não explica sozinha os defeitos principais.
- Último contador: 1984 chamadas GetView recusadas como invalid_texture, zero como
  invalid_vertex/vertex, zero unsupported_format e 236 pending acumulados.
  São contagens de chamadas após caches de bindings; não são pixels ou recursos únicos.
- Cinco descritores recusados detalhados. Alguns são vazios, outros possuem dados;
  não inferir automaticamente que todos representam materiais obrigatórios.
- Nenhum novo crash foi relatado nesta execução. O final do log contém renderização
  e estatísticas, não uma conclusão explícita de encerramento normal.

## Perfil

Amostragem de pilhas ativa em 1 ms, ELF preservado correspondente ao NRO enviado.
analyze-profile.py simboliza 163 PCs das threads com pelo menos 5% de CPU; resultado
por intervalo em profile-summary.json. O perfil inclui nove cabeçalhos, um de arranque.

Nos últimos dois intervalos: 9,4 e 8,9 FPS; eGraphicsRender 92,6 e 96,7% de um núcleo.
PCs do guest incluem sub_825863D0 e helpers __imp____restgprlr_29/__imp__sub_825A0D08.
A thread GPU ring native passa 68,9/73,4% das amostras em svcWaitSynchronization.
As proporções das pilhas são tempo observado e não tempo ativo de CPU por função.
Não otimizar uma espera de kernel como se fosse computação pesada.

Log do renderer nos últimos intervalos: CPU111 ms/quadro, GPU71 ms/quadro, swap88 ms.
Há sobreposição entre espera, CPU e GPU; não somar como custos independentes.
Clocks registrados: CPU1020, GPU460,8, RAM1331,2 MHz; nenhum overclock aplicado.
Como a amostragem pausa threads, comparar desempenho com perfil desligado antes
de atribuir uma regressão exclusivamente à montagem de atlas.

O primeiro intervalo tem duração zero e taxas absurdas de chamadas por segundo.
É artefato da divisão pelo intervalo inicial, não tráfego real. Os contadores GPU
genéricos zerados no perfil não medem os draws do renderer nativo; usar seu próprio log.

## Próxima comparação local

1. Testar Vulkan com render_target_path_vulkan=fsi e descritores inválidos bloqueados.
   A GPU anuncia interlock, mas o SDK também verifica outras capacidades; suporte
   anunciado à extensão sozinho não garante seleção efetiva do caminho.
2. Build D3D12 isolado com o mesmo SDK, fontes geradas e flags de compilação do
   Vulkan atual. O PC antigo funcional permanece preservado. Isso reduz a confusão
   entre mudança de backend e mudança de versão do SDK/código.

Se FSI corrigir os materiais, priorizar diferenças de interpretação/packing dos
render targets e resolves no caminho simplificado. Se só D3D12 atual corrigir,
comparar DXBC/SPIR-V e entradas pelo hash do shader. Se ambos continuarem errados,
investigar as diferenças comuns contra o PC antigo antes de alterar o Switch.
Nenhum desses resultados, sozinho, confirma que todos os defeitos tenham a mesma causa.

Não preparar outra tentativa no console apenas trocando RAM ou aceitando descritores.

## Builds preparados

D3D12 atual e Vulkan atual compilaram/linkaram com sucesso. Hashes dos executáveis
e bibliotecas em pc-build-artifacts.json. SDK agora aceita REXGLUE_OUTPUT_DIRECTORY;
cada build usa seu próprio sdk-bin. Isso evita colisão do caminho global de saída
do SDK entre configurações de backend. Primeira tentativa D3D12 teve falha de link
durante colisão; pc-d3d12-isolated-build.txt registra a reconstrução concluída.
pc-vulkan-isolated-build.txt registra a reconstrução Vulkan concluída.

Vulkan passou a registrar requested/selected/fsi_supported em [gl-pc-compare],
para não confundir solicitação de FSI com seleção efetiva. O launcher específico
desliga o bypass inválido e usa o mesmo user-vulkan da comparação anterior.
D3D12 possui launcher separado com render_target_path_d3d12=rtv explícito e o mesmo
diretório de usuário do Vulkan. Os testes visuais desses novos caminhos estão pendentes.
