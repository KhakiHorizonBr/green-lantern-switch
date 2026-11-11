# Retomada do port experimental

O desenvolvimento local foi encerrado em 11/11/2026. O código recompilado chegou ao gameplay no Switch com Vulkan/NVK e no protótipo deko3d. O desempenho segue abaixo da meta na fase pesada. O pacing da versão Vulkan limita cenas leves a 30 FPS.

| Etapa | Evidência e decisão |
| --- | --- |
| Boot, memória e comparação com PC | O histórico registra o runtime Horizon, heap de 2000 MiB, arena de 384 MiB e os caminhos D3D12, Vulkan padrão e Vulkan adaptado. [Diário técnico](PORT_LOG.md) |
| Imagem no Switch | O teste 068 confirmou a correção dos principais defeitos visuais observados. Os ajustes de depth/stencil, EDRAM e fetch de vértices sustentam esse resultado. [Resultados 071–074](RESULTADOS_071_074.md) |
| Instrumentação e pesquisas | Temporização por quadro, auditoria de ports Xbox 360, submissões parciais e cache de geometria. O cache residente foi verificado no PC e ficou experimental. [Rodadas de outubro](RESULTADOS_2026_10_27_29.md) |
| Submissão parcial 086/087 | Limite 256 reduziu a espera no alocador, mas ela reapareceu em outros pontos. Sem ganho sustentado na fase pesada. Padrão 0. [Logs](../logs/run-086-087/) |
| Vulkan 088/089 | O 088 registrou 24–25 FPS em trechos leves e janelas prolongadas de 7–9 FPS em trechos pesados. O scissor reduzido baixou o total de GPU de 73,1 para 54,0 ms em amostras com volume próximo de draws, com imagem cortada. [088](../logs/run-088/RESULTADO_088.md) · [089](../logs/run-089/RESULTADO_089.md) |
| Escala interna 092 | A versão de 75% manteve a imagem inteira no PC após alinhar a resolução dos passes que compartilham profundidade. No Switch, as conversões frequentes consumiram a economia de pixels. Manter 100%. [092](../logs/run-092/RESULTADO_092.md) |
| deko3d 075–085 e 090 | O NRO 009 chegou ao gameplay no 085, com janelas pesadas de aproximadamente 2 FPS, variantes de shader ausentes e teto interno de imagens atingido. O 090 confirma deko3d v9 e não serve para avaliar a escala Vulkan. [Protótipo e dependências](../experiments/deko3d/README.md) |

## Reconstrução local

1. Use a revisão ReXGlue indicada em [SDK_BASE.txt](../SDK_BASE.txt) e inicialize os submódulos. Copie [sdk-overlay](../sdk-overlay/) sobre a revisão. A [nota de procedência](BUILD_AND_PROVENANCE.md) dá os créditos e registra o ambiente original. O patch mais antigo permanece no histórico. O overlay final contém os arquivos novos de que o Switch depende.
2. Forneça uma cópia local dos arquivos do seu jogo Xbox 360. Ajuste os caminhos nos [manifestos](../project/). Rode o gerador ReXGlue com `rexglue codegen green_lantern_hinted_manifest.toml` a partir da pasta `project`, com o XEX disponível no caminho indicado no manifesto. O CMake do Switch espera `project/generated/hinted/sources.cmake`. A configuração de desktop também espera o bootstrap do SDK em `project/generated/rexglue.cmake`. Consulte a documentação do ReXGlue da revisão fixada para gerá-lo.
3. Instale devkitA64, libnx, CMake e Ninja. Prepare Mesa Switch em `d4a00ea0ab3f59afb967cc5d779e4263d237bd77`, com [overlay Mesa](../experiments/mesa-switch-overlay/) e o procedimento histórico de empacotamento [NVK](MESA_NVK_SETUP.md). O SDK local precisa dos archives NVK e NAK. Docker era uma ferramenta de build do Mesa.
4. Configure [generated_full](../project/switch/generated_full/CMakeLists.txt) com `REXGLUE_SDK_ROOT` e `REXGLUE_SWITCH_NVK_SDK` apontando para as dependências locais. Use o [toolchain Horizon](../project/switch/toolchain-horizon.cmake). O alvo Vulkan do jogo é `green_lantern_switch_game`. A aplicação busca os arquivos em `sdmc:/switch/green_lantern/game`.
5. Para a experiência deko3d, aplique o overlay descrito em [experiments/deko3d](../experiments/deko3d/README.md). Os shaders do jogo precisam ser capturados e convertidos localmente.

As receitas de laboratório em `experiments/` reaproveitavam objetos compilados e caminhos daquela máquina. Os marcadores `<WORKSPACE>` e `<USER>` indicam caminhos a ajustar. A publicação preserva fontes e evidências, sem atestar um build limpo da árvore publicada. Arquivos do jogo, C++ gerado do executável, shaders extraídos, saves e binários de entrega são insumos ou saídas locais. Os logs maiores são representados por suas análises ou por excertos identificados.

## Continuação técnica

Comece com Vulkan a 100%, `ablation_mode=0` e `partial_submit_draws=0`. Reproduza a mesma fase em condições próximas de clock e compare tempo de GPU e esperas da thread gráfica. Para retomar a escala, mantenha as imagens menores entre os passes e materialize o tamanho original quando um resolve, alias EDRAM ou outro consumidor realmente precisar dele. Valide profundidade, stencil e tempo total antes de promover a variante. No deko3d, a cobertura de shaders e a redução das esperas de fila são os pontos em aberto documentados no 085.
