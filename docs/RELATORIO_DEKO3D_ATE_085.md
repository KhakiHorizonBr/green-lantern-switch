# Green Lantern para Switch — relatório da migração para deko3d

**Atualizado em 29/10/2026, até o teste de hardware 085 / NRO 009.**

Este documento reúne a implementação, os testes, as regressões e os problemas pendentes da etapa deko3d. Foi preparado para leitura pelo usuário e para revisão por outro desenvolvedor. Descreve o workspace local; não confirma que todas essas alterações já tenham sido publicadas no GitHub.

## 1. Resumo para quem não trabalha com programação

Conseguimos trocar o caminho que desenha o jogo no Switch por uma implementação experimental usando deko3d. O jogo passou a abrir as telas iniciais, a imagem deixou de aparecer cortada e, segundo o teste 085 do usuário, chegou ao jogo. O desempenho continua muito baixo.

O estado atual **não é um port jogável concluído**. Há três problemas separados:

1. **Esperamos a GPU terminar depois de praticamente cada desenho.** Isso evita a regressão observada ao agrupar desenhos, mas cria centenas de envios e esperas em cada quadro.
2. **Faltam shaders de algumas cenas.** O pacote contém 122 variantes previamente capturadas. O teste 085 encontrou mais 26 combinações de estágio/hash/modificação ausentes. Nesses casos, o renderer pula o desenho.
3. **O orçamento interno de imagens atingiu 384 MiB.** O log registra falhas ao criar texturas e outros alvos. Isso precisa ser resolvido ou dimensionado antes de avaliar a qualidade e o desempenho de uma fase completa.

Em uma janela das telas iniciais, o NRO 009 chegou a 29,50 FPS. No trecho posterior e mais pesado, as últimas janelas ficaram aproximadamente entre 1,96 e 2,39 FPS. Os 29,50 FPS **não representam o desempenho da fase**.

## 2. Por que começamos a usar deko3d

Antes desta etapa, o renderer Vulkan adaptado já tinha alcançado imagem correta em testes do projeto, mas o desempenho no Switch ainda era insuficiente. Como referência histórica, a documentação do hardware 074 registrava cerca de 24–25 FPS no menu e 7,3–8,8 FPS em uma cena pesada.

A proposta do deko3d foi experimentar um caminho gráfico próprio do Switch, com controle direto de comandos, memória, imagens e sincronização. Isso oferecia uma alternativa para reduzir custos de CPU e do caminho Vulkan/NVK. **A troca de API, por si só, não garante aumento de FPS.**

O trabalho anterior com outros ports ajudou a orientar a arquitetura. O renderer Vulkan adaptado do caminho usado nos projetos de referência, incluindo o dos Simpsons, permaneceu importante como fonte da matemática de gráficos e como comparação visual. O snapshot local `work/deko3d-sdk-reference` forneceu a base de dispositivo, fila, gravador, memória e apresentação do deko3d.

O renderer completo desse SDK não foi simplesmente conectado ao jogo: sua biblioteca de shaders e seus layouts HLSL não correspondiam ao contrato de constantes do Lanterna. Foi necessário construir um adaptador próprio.

## 3. O que está rodando de fato

O código recompilado do Xbox 360 continua executando a lógica do jogo. O parser PM4 lê os comandos gráficos produzidos por ele. No target deko3d, esses comandos são encaminhados para `deko::GameRenderer`, que prepara recursos e emite chamadas deko3d.

Fluxo simplificado:

```text
Código recompilado do jogo / memória guest
                 ↓
Parser PM4 e espelho de registradores
                 ↓
GameRenderer deko3d
  ├─ seleção de shaders AOT por estágio + hash + modificação
  ├─ constantes, índices e fetches de vértices
  ├─ texturas, alvos, aliases de EDRAM e stencil
  └─ draws, clears, resolves e apresentação
                 ↓
Gravador / fila Graphics + Compute deko3d
                 ↓
Driver NVIDIA do Switch / GPU
```

É um backend próprio para os comandos gráficos do jogo, não uma reescrita completa do renderer de alto nível da engine. Continua sendo necessário reproduzir o comportamento gráfico do Xbox 360.

O target atual é `green_lantern_switch_deko`, com a opção `GREEN_LANTERN_BUILD_DEKO_VARIANT=ON`. Ele não cria um dispositivo Vulkan para renderizar. O executável ainda liga componentes comuns do SDK e pode conter símbolos Vulkan, porque reutiliza partes de CPU e estruturas existentes.

O NRO Vulkan anterior foi preservado. Não substituímos todos os caminhos do projeto por deko3d.

## 4. Shaders: do contrato Vulkan para DKSH

### 4.1 Captura e primeira barreira

Foi montado um corpus de 122 variantes de shaders do próprio jogo. A tentativa inicial de conversão rejeitou todas por incompatibilidades nos layouts de buffers.

Um exemplo importante foi `XeSystemConstants`: o contrato original tinha 504 bytes e offsets explícitos que não correspondiam diretamente ao layout std140 usado pelo novo caminho. Não bastava trocar a extensão ou recompilar o mesmo texto.

### 4.2 Adaptação implementada

Foram criados layouts e packers que transformam as constantes originais para um contrato compatível. O bloco de sistema passou de 504 para 528 bytes no upload adaptado. Os demais buffers, bindings, máscaras e tamanhos também foram refletidos e verificados.

Foi implementada uma adaptação restrita para descarte adiado em seis pixel shaders, com rejeição dos casos não aceitos. Não foi assumida equivalência para qualquer shader arbitrário.

Após essas adaptações, as 122 variantes do corpus foram convertidas e compiladas com `uam` para DKSH.

### 4.3 Biblioteca entregue ao Switch

O pacote inicial continha:

- 122 módulos do jogo, selecionados por estágio/hash/modificação;
- 77 módulos auxiliares, incluindo primitivas, apresentação, cópias e conversão de profundidade;
- 199 arquivos DKSH no total;
- `modules.txt`, com o contrato necessário para preparar cada variante.

O caminho é AOT: a compilação dos shaders acontece no PC. Não existe compilação geral de novos shaders pelo `uam` dentro do NRO.

Quando uma variante não está no manifesto, o código registra `missing shader` e pula o desenho. Não escolhe silenciosamente outro shader com aparência parecida.

## 5. O que foi validado no PC

O PC foi usado para:

- executar o renderer Vulkan com o ABI adaptado das constantes;
- comparar o resultado visual desse caminho com a referência;
- fazer replays de menu até fase por 150 segundos;
- converter/refletir shaders e compilar DKSH;
- testar planos de upload, índices, alinhamento, tamanhos e limites;
- testar o decoder de texturas compartilhado;
- testar os cálculos de apresentação, crescimento dos alvos e agrupamento de tiles.

Os replays PC registrados concluíram com saída 0 e imagens inspecionadas corretas para os trechos comparados. Eles continuavam usando **Vulkan**. A GPU do PC não executou o backend deko3d.

Isso diferencia três tipos de evidência:

| Evidência | O que confirma | O que não confirma |
|---|---|---|
| Teste CPU no PC | Cálculos, layouts, limites e mapeamentos testados | Execução dos comandos na GPU do Switch |
| Compilação/link/NRO válido | O target gera um executável e recursos estruturalmente aceitos | Que uma cena renderiza ou é rápida |
| Teste no Switch | Comportamento observado naquela execução | Cobertura de todas as fases e estados gráficos |

Os testes CPU incluíram 768 combinações de textura, além de casos CTX1, packed16, limites e sentinelas. Mais tarde foram acrescentados testes de MRT esparso, apresentação e 1.024 combinações de pitches para cópias de tiles.

## 6. Recursos implementados no backend

### Dados de desenho

Constantes de sistema, floats VS/PS, bool/loop e fetch são preparados conforme o contrato refletido. Índices e memória de vértices são enviados por staging. A arena de vértices tem 160 MiB, com relocação dos fetches para offsets dessa arena.

O caminho utiliza hashes e o `ChangeDetector` já empregado no renderer Vulkan para evitar reler integralmente buffers grandes em todo draw. Cópias e atualizações são ordenadas por comandos de GPU e barreiras.

### Texturas

Foi extraído um decoder CPU compartilhado com o Vulkan. Ele trata os formatos implementados, endianness, mipmaps, swizzles, arrays/cubemaps e uploads por nível/camada.

Entre as correções dessa extração estão a expansão DXT3A de bloco guest de 8 bytes para bloco host de 16 bytes, o gather de packed16 e a contagem de palavras em `k16in32` no SDK. O patch da última correção foi preservado no experimento.

Não há suporte geral completo a texturas 3D nem a todos os formatos. Alguns casos são recusados explicitamente.

### Alvos, profundidade e EDRAM

Foram conectados rasterização, blend, depth/stencil, máscaras de escrita, viewport/scissor, clears e resolves.

Os padrões conhecidos de aliases de cor, inicialização de EDRAM, conversão de profundidade para R32 e palavras RGBA8 e restauração de stencil foram adaptados do comportamento do renderer Vulkan. Os descritores e recursos temporários têm descarte associado aos slots/fences.

Essa adaptação não representa implementação universal de toda a EDRAM/MSAA do Xbox 360. Existem limitações, inclusive em certas conversões, memory export e auxiliares de geometria com clip/cull.

### Apresentação

O backend possui swapchain deko3d própria, saída 1280×720 e aplicação da rampa gamma. O endereço e swizzle do fetch0 e a área visível do frontbuffer passaram a ser considerados, aproximando o comportamento do caminho Vulkan.

## 7. Linha do tempo dos NROs e testes

| NRO | Hardware relacionado | Mudança principal | Resultado observado |
|---|---|---|---|
| 001 | 075 | Primeiro NRO do jogo com PM4 conectado ao deko3d e pacote DKSH | Inicializou dispositivo, recursos e swapchain, mas fechou antes de imagem visível. |
| 002 | 076 | Corrigiu slots vazios de MRT; ligou `libdeko3dd` e logs de arranque | Registrou falha de envio em `dkQueueFlush`; a causa final ainda não aparecia. |
| 003 | 077 | Checkpoints síncronos nos primeiros 64 draws; diagnóstico de kickoff | GPU concluiu 64 desenhos e 55 resolves; falhou depois que a verificação individual terminou. |
| 004 | 079 | Checkpoints contínuos; captura do ioctl interno de kickoff | Abriu as primeiras telas com metade inferior preta; caiu na transição ao Start, em compute. |
| 005 | 080 | Habilitou Compute na fila; ajustes de apresentação e fetch0 | Passou da falha de compute. Persistiram corte e outro timeout, agora identificado com código original. |
| 006 | 081 | Limitou kickoffs a 64 entradas; acrescentou dimensões de draws/cópias ao log | Timeout persistiu com 64 entradas. Logs expuseram alvos limitados a 352 linhas. |
| 007 | 082 e arquivos rotativos | Crescimento de alvos e agrupamento horizontal de cópias de EDRAM | Usuário confirmou imagem inteira e ausência de crash. Muito lento; não esperou a leitura de save terminar. |
| 008 | 084 | Retirou diagnóstico por draw e agrupou até 64 operações | Melhorou nas telas leves, mas reintroduziu timeout antes do Start. Regressão. |
| 009 | 085 | Restaurou sincronização individual; manteve logs enxutos | Usuário relatou que chegou ao jogo, ainda muito lento. Log trouxe pressão no orçamento de imagens e shaders ausentes. |

Os números de hardware e os sufixos de NRO são identificadores diferentes. Não são uma sequência um-para-um: não há evidência analisada de todas as numerações intermediárias.

## 8. Falhas específicas encontradas e correções

### 8.1 Lista de alvos com posições vazias

O código podia preencher RT1/RT2 e deixar posições anteriores nulas. A API deko3d percorre os slots dentro da contagem ativa e não aceita esse formato de lista.

O adaptador passou a fornecer uma view válida nas lacunas, com escrita e blending desligados nelas, preservando os índices dos outputs. Testes CPU cobriram MRT esparso, desenho somente de profundidade e contagens inválidas.

Era um defeito confirmado no código. Não foi demonstrado como causa de todos os primeiros fechamentos.

### 8.2 Fila sem Compute

O hardware 079 produziu acesso nulo em `ComputeQueue::bindShader`. A fila era criada com Graphics, mas sem a flag Compute. O deko3d só inicializa sua estrutura de compute quando essa flag está ativa.

A criação passou a usar `Graphics | Compute`. O teste 080 concluiu a etapa que antes falhava. A correção foi preservada em `queue_compute.patch` e no snapshot local do SDK.

### 8.3 Timeout e caminho interno de envio

O código original do erro passou a aparecer após interceptar `nvioctlChannel_KickoffPb`. Interceptar apenas `nvGpuChannelKickoff` não cobria o kickoff chamado internamente pelo próprio objeto da libnx durante `AppendEntry`.

O retorno observado foi `0xD5C`, módulo 348, descrição 6: `LibnxNvidiaError_Timeout`. Não havia notificação detalhada de falha GPU disponível naquele instante.

Foi tentado limitar o lote de 886 para 64 entradas usando o gerenciamento normal de fences da libnx. O 081 mostrou timeout mesmo com 64. Portanto, o tamanho do lote isoladamente não explicou a falha.

### 8.4 Metade inferior preta

O usuário esclareceu que faltava a metade inferior do desenho; a imagem não estava apenas comprimida.

Primeiro foi corrigido o uso da área visível na apresentação, mas o problema persistiu. O log posterior mostrou frontbuffer 1280×720 e, em draws, viewport 1280×720 sendo limitado por alvo/scissor 1280×352.

O defeito era que `TargetFor` retornava uma imagem existente sem permitir crescimento. O resolve então era limitado pela altura antiga. O renderer Vulkan já tinha um caminho para aumentar o alvo quando uma cópia exigia mais linhas.

O 007 implementou crescimento alinhado, sem encolhimento posterior, preservando o conteúdo anterior de cor/depth/stencil e descartando a memória antiga por fence. O usuário confirmou imagem inteira no 082.

### 8.5 Muitas cópias pequenas de EDRAM

O import de aliases emitia cópias e barreiras individualmente para tiles de 80×16 pixels. Foi implementado agrupamento horizontal de tiles compatíveis, sem atravessar linhas de origem/destino, mudanças de produtor/estado ou o limite de EDRAM.

O buffer usado nessa etapa é scratch. A faixa agrupada utiliza pitch linear maior para a cópia de ida e de volta. Testes CPU compararam o mapeamento com o caso individual em 1.024 combinações de pitches.

O 082 passou sem crash com essas mudanças e sincronização individual. Isso não demonstra que todos os timeouts foram resolvidos independentemente da sincronização.

## 9. A lentidão do diagnóstico e a regressão ao tentar removê-la

O teste 082 gerou 14.765.724 bytes de logs em 652,673 segundos. Foram encontrados 47.356 pares de checkpoint, com avanço até frame 458, draw 34.118 e resolve 13.238.

Os intervalos entre os registros de início e conclusão somaram 434,379 segundos, mediana 4 ms e p95 12 ms. Esses intervalos incluem logging, submissão e execução/espera GPU: **não são uma medição de custo puro do log, nem uma economia garantida**.

O 008 retirou as paradas por desenho e agrupou operações. Atingiu uma janela de 29,35 FPS nas telas iniciais, mas caiu antes do Start no 084.

O 009 restaurou a conclusão individual, inclusive em resolves que só limpam alvos sem incrementar o contador de cópias. Os registros repetitivos continuaram desligados. Esse é o caminho do teste 085.

**Conclusão confirmada:** o agrupamento do 008 regrediu a execução. **Ainda não identificado:** qual recurso, barreira, estado ou regra de reutilização permite execução individual e falha no agrupamento. Não está provado que a solução seja apenas diminuir outro contador de lote.

## 10. Estado medido no log 085

O boot confirma NRO `deko-game-v9`, 122 variantes, `per_draw_diagnostics=false` e `batch_ops=1`. Não há erro fatal de kickoff/deko3d registrado no arquivo analisado. O usuário relatou que o jogo rodou, com FPS muito baixo.

Foram extraídas 29 janelas de desempenho. Exemplos:

| Hora no log | Quadro | FPS da janela | Draws/quadro | Resolves/quadro | Submissões/quadro | Espera explícita de fila/quadro | Imagens |
|---|---:|---:|---:|---:|---:|---:|---:|
| 18:52:26.835 | 209 | 29,50 | 18,1 | 3,9 | 23,1 | 21,16 ms | 117,7 MiB |
| 18:53:49.644 | 922 | 2,25 | 734,5 | 49,2 | 784,8 | 227,37 ms | 372,4 MiB |
| 18:54:41.210 | 1033 | 1,96 | 901,7 | 46,0 | 950,9 | 254,94 ms | 383,0 MiB |
| 18:54:46.506 | 1044 | 2,08 | 901,6 | 46,0 | 950,7 | 256,02 ms | 384,0 MiB |

As janelas leves e pesadas não são a mesma cena. O FPS é medido pela progressão de Swap em uma janela de tempo, não por captura do scanout do display.

Na última janela de aproximadamente dez segundos, o relatório antigo também registrou 20.424 chamadas de draw, 19.084 desenhos executados, 1.340 pulados e 20.115 submissões para 21 swaps. O intervalo entre conclusões de Swap teve p50 de 482,345 ms, p95 de 549,181 ms e p99 de 575,590 ms.

### 10.1 Limite de imagens atingido

No mesmo arquivo aparecem:

```text
texture upload/budget failed
target allocation failed
sized resolve allocation failed
resolve destination allocation failed
```

O valor `image_mib` chegou a 384,0, que é o teto configurado do gerenciador `Images`.

Isso **não demonstra esgotamento dos 4 GB físicos do Switch nem do limite inteiro do processo**. É um orçamento interno específico. Também não é a soma exata de toda a memória de GPU: existem arenas, descritores, buffers, chunks e recursos fora desse contador.

### 10.2 Shaders ausentes

Foram registradas **26 variantes ausentes**, identificadas por estágio, hash e modification. Duas modificações do mesmo hash contam como variantes diferentes.

Esses desenhos são pulados. Não é correto tratar a cobertura gráfica do gameplay como completa só porque a tela inteira voltou a aparecer. Também não é correto usar ganho de FPS obtido por omitir desenhos como otimização válida.

### 10.3 Limitações dos contadores

Alguns campos herdados do profiler Vulkan aparecem zerados no caminho deko3d porque ainda não foram instrumentados. Exemplos incluem pipelines Vulkan, partes do tempo por seção e timestamps GPU.

**GPU 0,0 ms nesses campos não significa GPU ociosa.** Para esta etapa, os resumos `[deko-perf-v8]` e os erros do backend são mais úteis, respeitando suas próprias limitações. `queue_idle_wait_ms_per_frame` mede espera de conclusão da fila, não tempo GPU por passe.

## 11. Orçamento e configurações atuais

| Área | Configuração atual | Observação |
|---|---:|---|
| Heap total | 2000 MiB | Não aumentada nos NROs deko desta sequência |
| Arena de backing guest | 384 MiB | Distinta do orçamento de imagens |
| Arena de vértices deko | 160 MiB | Fetches relocados; não é buffer completo de 512 MiB |
| Upload/staging | 3 × 32 MiB | Slots com fences |
| Orçamento de imagens | 384 MiB | Atingido no 085, com falhas de alocação |
| Scratch de aliases EDRAM | 10 MiB | Cópias agrupadas de tiles |
| Código de shaders do gravador principal | 16 MiB | Fora do contador de imagens |
| Lote GPFIFO | até 64 entradas | Limite mantido; sozinho não resolveu timeout |
| Sincronização de operações | individual por padrão | `deko_batch_ops=1` |
| Logs individuais | desligados por padrão | `deko_diagnostic_each_draw=0` |

Os limites não são parcelas de memória física independentes que possam ser somadas ou aumentadas sem considerar o allocator e o driver. O projeto ainda não possui um gerenciador automático completo que redistribua memória entre todas essas áreas.

O mapeamento de controles por posição de Xbox foi mantido: Switch B→Xbox A, A→B, Y→X e X→Y. Essa sequência de NROs não tentou aumentar clocks nem desbloquear o limite de FPS do jogo.

## 12. Próximas ações recomendadas, ainda não executadas

### A. Completar a cobertura gráfica e explicar as alocações recusadas

1. Localizar/capturar as 26 variantes ausentes e compilá-las no PC para o pacote DKSH.
2. Verificar quais imagens, targets, resolves e caches permanecem vivos ao atingir 384 MiB.
3. Separar recursos ativos, recursos aposentados aguardando fence e retenção/fragmentação de chunks.
4. Implementar ou ajustar descarte/reutilização de recursos que realmente ficaram sem uso.
5. Só então decidir se o teto de imagens precisa aumentar e de onde virá a memória.

Elevar o teto pode ser um experimento posterior, mas não explica por si só a retenção dos recursos nem os shaders ausentes.

### B. Reduzir o custo de sincronização mantendo o ponto de conclusão correto

O 085 registra quase uma submissão por desenho/resolve. Esse é um alvo concreto.

Uma hipótese de implementação é esperar o fence já gravado pelo `Nucleo::TerminarTrabajo`, em vez de fazer uma espera global de queue-idle que pode adicionar trabalho/fence/flush redundante. Isso precisa ser conferido no código e validado; não foi implementado nesta sequência.

Em paralelo, investigar a regressão do batching comparando vida útil de descritores, UBOs, memória de vértices, imagens e estado entre operações. Ampliar lotes novamente sem identificar essa dependência pode só repetir o timeout do 084.

### C. Medir desempenho por cena com a cobertura correta

Separar telas iniciais, menu, carregamento, cutscene e gameplay pesado. Registrar FPS, p50/p95/p99 de frame time, submissões, espera, custo de preparação e memória por fase.

Ainda não há timestamps GPU deko por passe suficientes para concluir que o gargalo restante é somente GPU, CPU, banda ou resolução. A quantidade de submissões/esperas já demonstra custo relevante, mas não explica todo o quadro.

O alvo imediato continua sendo uma execução estável e próxima de 30 FPS no gameplay. Nenhum resultado atual permite prometer esse desempenho ou 35–40 FPS.

## 13. Onde estão os arquivos principais

Raiz do workspace: `<WORKSPACE>`.

| Caminho relativo à raiz | Conteúdo |
|---|---|
| `work/green-lantern-360/project/switch/deko_experiment/` | Backend, helpers, planos, shaders auxiliares, scripts e patches |
| `.../deko_experiment/deko_game_renderer.cpp` | Adaptação dos comandos do jogo e recursos para deko3d |
| `.../deko_experiment/deko_bindings.cpp` | Contrato de bindings e gravação dos draws |
| `.../deko_experiment/deko_images.cpp` | Imagens, descritores, cópias e vida útil |
| `.../deko_experiment/deko_depth.cpp` | Conversão compute de profundidade/stencil |
| `.../deko_experiment/deko_output.cpp` | Apresentação/swapchain/gamma |
| `.../deko_experiment/target_plan.h` | Crescimento de targets e planejamento de faixas de tiles |
| `.../native_experiment/native/native_system.cpp` | Parser PM4 e seleção do renderer por target |
| `.../native_experiment/native/native_texture_cpu.h` | Decoder CPU compartilhado |
| `work/deko3d-sdk-reference/src/ui/deko3d/nucleo.cpp` | Dispositivo, fila, gravador e memória do snapshot de referência |
| `work/green-lantern-360/project/switch/generated_full/CMakeLists.txt` | Target deko/NRO e flags de link |
| `work/green-lantern-360/project/switch/PORT_STATUS.md` | Histórico do projeto; topo mais recente deve prevalecer |
| `outputs/deko-native/hardware-085/` | Log original e análise estruturada do teste atual |
| `outputs/deko-native/hardware-082/analysis.json` | Quantificação dos checkpoints da versão intrusiva |
| `outputs/green-lantern-deko-009/` | Último NRO entregue, validação e instruções |
| `outputs/green-lantern-deko-007/` | NRO que passou no teste 082, com diagnóstico completo |
| `outputs/green-lantern-deko-package/` | Primeiro pacote completo com shaders e rollback Vulkan |

As tabelas usam caminhos relativos para facilitar a revisão dentro do workspace. Os artefatos de entrega ficam em `outputs`, não devem ser confundidos com publicação automática no GitHub.

## 14. Instalação e identificação da versão atual

O NRO atual é `outputs/green-lantern-deko-009/green_lantern_deko.nro`. O identificador NACP é `0.1.0-deko9` e o boot registra `[deko-game-v9]`.

No SD, o NRO e a pasta `deko_shaders` ficam em `sdmc:/switch/green_lantern/`. O jogo permanece na pasta `game` existente. A abertura usada é homebrew em modo aplicativo, pelo R ao iniciar um jogo.

Os shaders não mudaram entre os NROs 001 e 009 desta sequência. A futura inclusão das variantes ausentes exigirá atualizar o manifesto e os DKSH correspondentes; não basta entregar outro NRO nesse caso.

Para revisão pública, compartilhar fontes, patches e relatórios. O XEX, arquivos do jogo, saves e shaders capturados da cópia local permanecem fora do repositório público.

## 15. Cuidados para continuar sem repetir as rodadas anteriores

- Usar o 009/085 como referência atual de execução com logs enxutos, e o 007/082 como referência anterior com diagnóstico individual.
- Não usar o 008 como baseline estável: ele regrediu no 084.
- Não declarar que o corte veio de padding: a tentativa de apresentação não o resolveu; o crescimento de targets foi a mudança confirmada visualmente no 082.
- Não interpretar timeout `0xD5C` como prova automática de falta de RAM ou de shader inválido.
- Não afirmar que as 122 variantes cobrem o jogo inteiro: o 085 comprovou lacunas.
- Não tratar 29,50 FPS nas telas iniciais como FPS de gameplay.
- Não interpretar contadores herdados zerados como tempo real de GPU igual a zero.
- Não afirmar que o deko3d foi validado graficamente no PC. O PC validou CPU/ABI e replays Vulkan; a execução deko3d foi testada no Switch.
- Preservar o NRO e seu ELF correspondente, os hashes e o log de cada candidato para simbolizar um crash com o executável certo.

## 16. Nota sobre o que mudou com o teste 085

Antes do 085, a prioridade documentada era recuperar estabilidade com logs enxutos e medir o custo das esperas. Agora há evidência adicional de **cobertura de shaders incompleta e orçamento de imagens atingido**.

Portanto, o próximo trabalho não deve ser apenas aumentar contadores de lote ou desligar verificações indiscriminadamente. Precisamos estabilizar a cobertura de recursos e remover custos de sincronização com uma explicação verificável para a regressão do agrupamento.

O teste 085 é um avanço de alcance e de diagnóstico: o usuário chegou ao jogo e o arquivo não registra o crash fatal anterior. Isso ainda não significa que renderização, memória e desempenho estejam prontos.


## Anexo A — variantes ausentes registradas no 085

Esta lista foi extraída do log, sem reproduzir o microcódigo ou o conteúdo dos shaders. `v` significa vertex shader e `p` pixel shader. O identificador inclui a modificação, porque o mesmo microcódigo pode exigir mais de uma variante compilada.

| Estágio | Hash | Modificação |
|---|---|---|
| PS | `03FD9AE4A40314CC` | `00000D000000003F` |
| PS | `19609576E95E3209` | `00005100000000FF` |
| PS | `1E75454E4D5E68C5` | `00004000000000FF` |
| PS | `21D3ED7D0EA2C33C` | `00004000000000FF` |
| PS | `329813125A505A2E` | `00005100000000FF` |
| PS | `37D6C3165A564E5C` | `00005100000000FF` |
| PS | `639B0D2FA5BE0E04` | `00005100000000FF` |
| PS | `7F3850BBDD3618E1` | `00005500000003FF` |
| PS | `9D03455FD4F570B6` | `00005500000003FF` |
| PS | `A68D275A4A9CE153` | `00005100000000FF` |
| PS | `BC4FC6A0CFD98F88` | `000040000000003F` |
| PS | `CFDF3D187A823E0A` | `0000410000000000` |
| PS | `E5CA281D4FDD2244` | `00001300000001FF` |
| PS | `EF816C15798B652D` | `00005100000000FF` |
| PS | `F745674382D678CB` | `000009000000000F` |
| VS | `03CD6906D473459D` | `000000000000003F` |
| VS | `6779911BAF7DC3C7` | `0000000000000000` |
| VS | `A2225CC1761583A3` | `0000000000000001` |
| VS | `A2225CC1761583A3` | `0000000004000001` |
| VS | `A2FDFDE6BEAE14B3` | `00000000000000FF` |
| VS | `AAD478399CE42F4C` | `00000000000000FF` |
| VS | `C5FB67497206BF53` | `00000000000000FF` |
| VS | `C96F57E5FB162D56` | `0000000000000000` |
| VS | `CD9F15112BE4E6A7` | `0000000000000001` |
| VS | `CD9F15112BE4E6A7` | `0000000004000001` |
| VS | `F131ECC7011477E8` | `000000000000000F` |

## Anexo B — identificação e evidências locais

Último NRO entregue: **009 / 0.1.0-deko9**.

- Tamanho: 54,421,701 bytes.
- SHA-256: `3284bbd338c30c538ae24d6ed728a524b034c394d97681f357112d8f972ca1ba`.

- Análise estruturada do 085: `outputs/deko-native/hardware-085/analysis.json`.
- Log preservado: `outputs/deko-native/hardware-085/green_lantern_085.log`.
- Análise dos checkpoints do 082: `outputs/deko-native/hardware-082/analysis.json`.
- Testes CPU da correção de targets/tiles: `outputs/deko-native/draw-checks-007.txt`.
- Build do último NRO: `outputs/deko-native/build-nro-009.log`.
- ELF correspondente: `outputs/deko-native/green_lantern_deko_009.elf`.
- Metadados da entrega: `outputs/green-lantern-deko-009/validacao.json`.

Repositório público do projeto: <https://github.com/KhakiHorizonBr/green-lantern-switch>. O estado local descrito aqui pode estar adiante do publicado.

Fontes de API consultadas durante a investigação: código local dos headers/libnx, do SDK deko e do deko3d; referências públicas em <https://github.com/devkitPro/deko3d> e <https://github.com/switchbrew/libnx>. As conclusões de hardware deste relatório vêm dos logs e relatos do usuário, não de benchmarks de outros ports.

**Fim do relatório.**
