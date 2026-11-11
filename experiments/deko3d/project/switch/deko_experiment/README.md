## Estado consolidado até o hardware085 — 29/10/2026

Usuário chegou ao jogo no009, com FPS muito baixo. As últimas janelas do
085 ficaram perto de2FPS, com~950submissões/frame;26 variantes de shader
ausentes e orçamento Images384MiB atingido também exigem correção.
Relatório atual, cronologia e evidências: [RELATORIO_DEKO3D_ATE_085.md](../RELATORIO_DEKO3D_ATE_085.md).
As seções abaixo preservam o histórico e não substituem esse estado atual.

## Hardware084: regressão com batching; candidato009 — 29/10/2026

084 confirma008/per_draw_diagnostics=false/batch_ops64. Timeout0xD5C
regrediu na transição pré-Start. ELF008: Checkpoint → TerminarTrabajo →
appendGpfifoEntries. Cap64 aplicado. Não confundir com antigo crash compute.
Janela5s das telas iniciais atingiu29,35FPS, sem alcançar menu/gameplay;
falhou depois, portanto não há ganho estável declarado.

009 restaura fronteiras individuais de conclusão do007, preservando logs
enxutos do008. Padrão deko_batch_ops=1; inclui checkpoints de resolve com
apenas clear, mesmo sem incrementar stats.resolves. Batch2..64 passa a knob
experimental explícito. deko_diagnostic_each_draw continua0 por padrão, com
1 disponível para registros intrusivos. Mesmas arenas/heap/shaders/compute,
targets crescentes e cópias EDRAM coalescidas; resumo5s continua ativo.

NRO outputs/green-lantern-deko-009/green_lantern_deko.nro; só substituir NRO.
Se um tuning.ini foi manualmente alterado para batch64, remover ou usar1;
o usuário não precisou alterar arquivo para008, então padrão novo basta.
Build-nro-009.log e validação no pacote. Hardware-084 e ELF008/009 preservados.
009 ainda não executado no console. Batching não corrigido na raiz; restaurada
a sincronização que passou no082, para medir a retirada dos logs separadamente.

---

## Hardware 082: imagem inteira, sem crash; candidato de desempenho 008 — 29/10/2026

Usuário confirmou imagem inteira e sem crash, mas lentidão extrema até a
tela de leitura de save. Logs rotativos .2/.1/.log totalizam14.765.724bytes,
652,673s; renderização avançou atéframe458/draw34118/resolve13238. Sem
timeout/kickoff fatal registrado. Não afirmar conclusão do save ou gameplay.
47.356 checkpoints before/completed; intervalos somam434,379s, mediana4ms,
p95=12ms. Incluem logging/submissão/GPU: não são custo CPU puro ou economia
garantida. Análise e logs em outputs/deko-native/hardware-082/analysis.json.

008 desativa por padrão diagnóstico/log/flush/idle após cada operação.
Checkpoint fecha a cada64 draws+resolves completos; Swap/Flush/upload também
podem fechar antes. Submit mantém queue idle após submissão, para não liberar
concorrência total junto desta mudança. Cap GPFIFO64, compute, altura dos
targets, imports agrupados, shaders, heap e arenas preservados.

Novo resumo [deko-perf-v8] a cada>=5s de Swap: FPS da janela, draws/resolve/
submits por frame, queue_idle_wait_ms/frame, imageMiB e skips. Não é timestamp
GPU por passe; FPS inicial pode incluir arranque. Sem ganho medido para008.
Rollback em runtime: deko_diagnostic_each_draw=1 no tuning.ini (padrão0).
O flag é lido uma vez no arranque e restaura checkpoints individuais.

NRO outputs/green-lantern-deko-008/green_lantern_deko.nro; só substituir NRO.
Build-nro-008.log e validação estrutural no pacote; ELF008 e NRO007 mantidos.
Próximo hardware deve medir o mesmo trecho sem o custo de47mil checkpoints;
não prometer30FPS nem tratar007 como benchmark do potencial do backend.

---

## Hardware 081: targets truncados; NRO deko3d 007 — 29/10/2026

081 confirma cap64 aplicado, mas timeout0xD5C persiste em 64 entradas.
Não continuar reduzindo o cap sem nova evidência. Logs mostram viewport
1280x720 e alvo/scissor1280x352, além de alvos1280x256 perto da falha.
Defeito: TargetFor retornava imagem existente sem crescer; Resolve limitava
y1 à altura antiga. Vulkan GetRenderTarget aceita min_height e cresce ao
resolver, preservando a imagem anterior. Deko não implementava isso.

007 implementa TargetHeight alinhado16/limitado2048, nunca encolhe; draw usa
limites iniciais de EDRAM como Vulkan, Resolve pode pedir imagem host maior.
Resize copia conteúdo anterior de cor ou host depth/stencil no mesmo formato,
com barriers e descarte fenced. Metadata Target/seen e produtores permanecem
estáveis. Isso corrige truncamento confirmado no código; visual no console
ainda precisa de confirmação.

Import EDRAM passa a agrupar runs horizontais por produtor/zero/dirty state,
sem cruzar linhas de origem/destino nem tile2048. Scratch usa pitch da faixa;
bytes de ida/volta preservados. Reduz centenas de comandos pequenos no lote
da falha. Eliminação do timeout ainda não confirmada.

Testes PC PASS:352->720/sem shrink/alinhamento/1024 pitches/fronteiras, além
de ABI/uploads/MRT/apresentação. Evidência draw-checks-007.txt. NRO gerado em
outputs/green-lantern-deko-007/green_lantern_deko.nro. Só trocar NRO; shaders,
heap, arenas, compute, cap64 e sincronização conservadora mantidos. Sem FPS
ou GPU desta versão medidos. Build-nro-007.log, hardware-081 e ELF007 preservados.

---

## Hardware 080: timeout com 886 entradas; NRO deko3d 006 — 29/10/2026

080 confirma compute habilitado e conclusão do resolve que caía em 079.
Erro posterior: nv kickoff 0xD5C, módulo348 descrição6 = Timeout (enum libnx),
886 entradas/flags0x104; nenhuma notificação GPU disponível nesse instante.
Não inferir falta de RAM nem hang específico de shader desse retorno sozinho.

006 limita cada kickoff a 64 entradas via wrapper de nvGpuChannelAppendEntry,
antecipando nvGpuChannelKickoff normal da libnx. Preserva o gerenciamento de
fences; não divide payload de ioctl manualmente nem faz retry cego de timeout.
Call sites do deko são ligados ao wrapper. Mudança ainda não validada no Switch.

Corte da imagem continua: usuário confirmou metade inferior ausente, não
imagem inteira comprimida. Log de apresentação era image=1280x720 e
visible=1280x720, portanto a hipótese de padding não explica essa captura.
Sem nova alteração gráfica às cegas: logs limitados de viewport/scissor/RT
e último retângulo copiado ao frontbuffer adicionados para separar origem,
resolve e apresentação. Foto do corte ajudará na próxima comparação.

NRO outputs/green-lantern-deko-006/green_lantern_deko.nro; só substituir NRO.
Shaders/heap/arenas iguais; compute e checkpoints síncronos mantidos. Build
outputs/deko-native/build-nro-006-final.log. Sem prova de correção do corte,
de eliminação do timeout ou FPS desta versão. Evidências em hardware-080.

---

## Hardware 079: causa de crash identificada; NRO deko3d 005 — 29/10/2026

079 abriu primeiras telas com metade inferior preta. GPU concluiu 2915
draws/990 resolves. Resolve seguinte causou acesso nulo no PC+0x1e7fcac:
ComputeQueue::bindShader (ELF 004), chamado por processCtrlCmd na submissão.
Causa no código: Dispositivo::Crear sobrescrevia flags padrão da fila com
Graphics e retirava Compute. O deko3d só inicializa m_computeQueue com essa
flag. Corrigido para Graphics|Compute. Patch do SDK em deko_experiment/queue_compute.patch.

Apresentação alinhada ao caminho Vulkan: fetch0 determina endereço/swizzle;
Swap width/height limitam a área visível, sem mostrar padding do resolve.
Imagem interna é escalada à saída 1280x720 do título; uma imagem 1280x360
não deve virar uma faixa de 360 linhas no display. Plano testado no PC:
crop/padding, baixa resolução, tamanho normal e fonte vazia, além dos
testes anteriores de MRT/uploads/ABI. Testes PASS; visual ainda não confirmado.

NRO: outputs/green-lantern-deko-005/green_lantern_deko.nro. Só trocar NRO:
shaders, heap e arenas não mudaram. Sincronização conservadora mantida,
sem alegação de FPS. Build-nro-005-final.log e validação no pacote local.
Evidências originais em outputs/deko-native/hardware-079; ELF 004/005 preservados.
Versão 005 compilada/verificada, ainda não executada no console.

---

## Hardware 077 e NRO de diagnóstico deko3d 004 — 29/10/2026

077: GPU completed até draws=64/resolves=55. Abort subsequente em
dkQueueSubmitCommands: gpfifo entry append failed, but no error was reported.
Stack simbolizada no ELF 003: appendGpfifoEntries → submitCommands →
Nucleo::TerminarTrabajo → GameRenderer::Swap. Não presumir OOM, erro no
desenho 65 ou sincronização como causa confirmada. A sonda 003 parava suas
esperas após 64 draws, deixando a próxima faixa sem isolamento individual.

004 mantém checkpoints por draw/resolve sem esse limite. Intercepta também
nvioctlChannel_KickoffPb, cobrindo kickoff interno a gpu_channel.c que não
atravessa __wrap_nvGpuChannelKickoff. Registra Result bruto e NvError no erro.
NRO: outputs/green-lantern-deko-004/green_lantern_deko.nro; só substituir NRO.
Heap/arenas/shaders preservados. Versão de diagnóstico, não benchmark.
Build outputs/deko-native/build-nro-004-final.log; pacote contém validação.
Sem teste hardware desta versão nem correção final confirmada. Logs originais
preservados em outputs/deko-native/hardware-077; ELF 003/004 preservados.

---

## Hardware 076: falha de kickoff; NRO diagnóstico deko3d 003 — 29/10/2026

076 confirma libdeko3dd ativa e abort em dkQueueFlush: "gpu channel kickoff
failed, but no error was reported". Stack atual simbolizada no ELF 002:
Nucleo::TerminarTrabajo → GameRenderer::Swap → parser PM4.
Inicialização e múltiplas submissões anteriores ocorreram. Causa final
ainda não identificada; não há prova de OOM nem de textura como causa.

003 gerado em outputs/green-lantern-deko-003/green_lantern_deko.nro.
Diagnóstico, não benchmark: primeiros draws/resolves com conclusão GPU
verificada, hashes por checkpoint e lotes posteriores de até 32 draws.
Wrapper nvGpuChannelKickoff registra Result original + NvNotification/NvError
no erro, antes de libdeko3dd abortar. FlushLogging preserva o último checkpoint.
Layouts de textura recusados agora incluem fetch/dimensão/formato/endereço.
Shaders/heap/arenas não mudaram; substituir só o NRO. Não testado no hardware.
Evidências: outputs/deko-native/hardware-076, build-nro-003-final.log e
outputs/green-lantern-deko-003/validacao.json. Pacotes anteriores preservados.

---

## deko3d 002 — revisão após fechamento no hardware 075 — 29/10/2026

Hardware 075: deko3d iniciou dispositivo/recorder, carregou 122 variantes,
criou swapchain e começou a processar o jogo. Fechou antes de imagem visível.
O log termina após alocações de imagens e um layout de textura recusado;
não identifica a causa final. Nenhum OOM explícito registrado. Crash e perfil
anexos contêm execuções antigas: não usar seus endereços/FPS como prova desta.

Defeito encontrado na revisão: MRT esparso deixava views nulas dentro de
color_count; dkCmdBufBindRenderTargets percorre/dereferencia todos os slots.
Corrigido sem compactar índices: preencher lacunas com view válida e
desativar escrita/blending nesses slots. Testes CPU no PC: PASS para MRT
esparso, depth-only, contagens inválidas e ranges/índices/ABI dos uploads.
O defeito é confirmado no código; causalidade do crash 075 não confirmada.

Nova versão liga libdeko3dd, com callbacks e FlushLogging, e breadcrumbs
limitados aos primeiros draws/submissões. Heap, shaders e arenas preservados.
NRO compilado: outputs/green-lantern-deko-002/switch/green_lantern/green_lantern_deko.nro.
Pacote: outputs/green-lantern-deko-002.zip. Versão NACP 0.1.0-deko2.
Build: outputs/deko-native/build-nro-002-final.log.
Ainda sem validação gráfica/FPS no hardware para 002; PC só valida lógica CPU,
pois não executa deko3d. NRO/pacote 001 e rollback Vulkan 074 preservados.

---

## NRO deko3d experimental 001 gerado — 29/10/2026

O target `green_lantern_switch_deko` compilou e empacotou o NRO real do jogo.
O parser PM4 agora despacha Draw/Resolve/Swap para `deko::GameRenderer` neste
target; a apresentação é própria e a criação do dispositivo Vulkan é pulada.
O target Vulkan existente foi preservado. O novo executável ainda liga
componentes comuns do SDK e pode conter símbolos Vulkan.

Pacote local: `outputs/green-lantern-deko-package/`, com NRO, manifesto,
122 variantes DKSH do jogo, 77 módulos auxiliares e rollback Vulkan 074.
Copiar a pasta `switch` inteira; `deko_shaders` é obrigatória.
Build final: `outputs/deko-native/build-nro-final.log`.
Validação estrutural e hashes: `outputs/green-lantern-deko-package/validacao.json`.

Conectados: ABI de constantes, shaders AOT por hash/modificação, fetches de
vértices relocados, texturas/índices, raster/blend/depth/stencil, auxiliares de
primitivas, apresentação/gamma, resolves de cor e profundidade, aliases de
cor e restauração de stencil dos padrões conhecidos do renderer Vulkan.
Heap 2000 MiB e backing 384 MiB preservados; vértices 160 MiB, uploads 3x32 MiB.

**Ainda sem execução GPU deko3d ou FPS medido no Switch.** Os testes PC
anteriores validaram CPU/ABI e replay Vulkan, não este backend deko3d.
Cobertura AOT limitada às variantes capturadas; variantes ausentes são
logadas/ignoradas. Texturas 3D, alguns formatos e conversões gerais de
EDRAM/MSAA e GS com clip/cull continuam limitados. Não declarar equivalência
visual ou ganho de desempenho antes do teste no console.

As seções abaixo são histórico das etapas anteriores; os avisos antigos
de "PM4 não conectado"/"sem NRO" descrevem aqueles momentos, não o estado atual.

---

# Experimento deko3d

Estado em 28/10/2026: **adaptação offline de constantes e compilação DKSH concluídas
para as 122 variantes capturadas; backend gráfico ainda não integrado**.

## Ligação dos uploads e resolves — 29/10/2026

`RecordUploadedDraw` conecta os dados CPU preparados ao `RecordDraw` real:
reempacota SystemConstants, sobe UBOs/SSBOs/índices e liga os ranges por
estágio. Um bloco compartilhado por VS/PS/GS é enviado uma única vez. O plano
de upload é independente da API (`draw_upload_plan.h`) e verifica máscaras,
tamanhos, alinhamento e limites dos índices antes de reservar. Se o recorder
precisar de outra submissão, retorna `NeedsSubmission` sem gravar comandos.
Os fetches de vértice já devem estar relocados para o SSBO fornecido; esta
função não transforma endereços absolutos da RAM em offsets de upload.

`DrawPacket` aceita geometry shader e seus bindings; isso permite ligar os
auxiliares de primitivas quando sua versão DKSH estiver disponível. **Não
significa que esses shaders auxiliares já foram convertidos ou conectados.**

`Images::InitializeDepthResolve` carrega um compute DKSH gerado a partir da
mesma fonte matemática do Vulkan por `build_depth_shader.py`. Após essa
inicialização, `Images::Copy` de depth/stencil 1x para R32 storage chama o
converter e preserva uma segunda imagem RGBA8 com os bytes guest. A imagem
de bytes está disponível por `PackedDepthWords(destination_id)` e acompanha
o descarte fenced do destino. `ImageSpec::guest_float20e4` deve refletir o
formato guest, não o formato usado pelo host para armazenar o depth.

Descritores compute são imutáveis durante a submissão e retornam ao pool
após reuso do slot de trabalho, com invalidação dos caches. O último slot de
sampler do recorder é reservado para os texelFetch desses resolves. Imagens
de destino precisam de `ImageSpec::storage=true`; MSAA e conversões de volta
para depth/stencil continuam retornando `NeedsConversion`.

Validação: testes CPU de ranges/ABI/índices passaram, assim como os 768 casos
de textura. Compute compilado pelo uam para DKSH (1280 bytes). Biblioteca
AArch64 e ELF de link-check ligam; símbolos `RecordUploadedDraw`,
`Images::Copy` e `DepthResolve::Record` presentes no ELF. Evidências locais:
`outputs/deko-native/depth/`. Nenhum desses comandos foi executado pela GPU
deko3d; a compilação não prova equivalência visual no Switch.

**O parser PM4 do jogo ainda chama o renderer Vulkan.** Ainda faltam a
preparação comum dos draws e a ligação do backend ao GraphicsSystem, além
dos auxiliares de primitivas, imports de aliases color/depth/stencil e
resolves MSAA. Não há NRO deko3d de gameplay pronto.

## Texturas compartilhadas e validação mais recente

`native_texture_cpu.h` contém agora a tabela de formatos e a conversão CPU
que o Vulkan realmente usa. O código não inclui tipos Vulkan. O deko3d usa
o mesmo decoder através de `deko_textures.cpp`, que prepara imagens, swizzles
e uploads por mip e face/camada. `Images` aceita mipmaps, cubemaps, arrays e
blocos BC1–BC5. Não há leitura de RAM como substituto para um resolve GPU:
o controle de aliases/EDRAM ainda precisa ser conectado separadamente.

Foram corrigidos três problemas reproduzidos com entradas sintéticas:

- DXT3A ocupa 8 bytes no guest e expande para um bloco BC2 de 16 bytes. O
  destino e o avanço entre blocos agora reservam esses 16 bytes.
- Pixels packed de 16 bits fazem gather da palavra correta quando a ordem
  de bytes exige uma troca de 32 bits.
- `CopySwapBlock(k16in32)` passava bytes a uma função que recebe quantidade
  de palavras de 32 bits: escrevia quatro vezes o tamanho pedido. Correção
  aplicada aos dois snapshots SDK locais e preservada em
  `texture_conversion_count.patch`. O snapshot deko, portanto, tem esta
  alteração local sobre o commit de referência, não é mais uma cópia intacta.

`run_texture_checks.ps1` compila e executa 768 casos de formato/endianness,
texturas lineares/tiled, mip tails e seis faces, além de CTX1, packed16,
spans insuficientes e sentinelas de saída. Resultado: PASS. As DLLs usadas
são copiadas do diretório `sdk-bin` recém-ligado, para evitar DLL antiga
ao lado do EXE mascarando o teste.

O replay PC final de 150 s (`outputs/native-pc-user/deko-textures-final`)
chegou à fase e saiu com código 0. Captura final inspecionada em
`outputs/deko-native/texture-replay-final.png`: personagem, cenário e HUD
corretos. É Vulkan executando o ABI adaptado; não comprova pixels DKSH,
desempenho Tegra ou equivalência byte a byte de animações independentes.

Build AArch64 concluído com deko3d/libnx reais, incluindo objetos do decoder
e upload. O link-check cobre bindings/imagens/apresentação; não executa nem
comprova a ligação de um jogo deko3d completo. C++23 e as declarações POSIX
exigidas pelo SDK são habilitados para os objetos de textura.

**Falta para o NRO deko3d do jogo:** o parser PM4 continua chamando o Vulkan;
preparação/bindings dos draws, primitivas auxiliares, conversões color↔depth/
stencil, aliases/EDRAM e resolves precisam usar os novos recursos. Packed
1555/4444 e texturas 3D são recusados explicitamente neste adaptador. Nenhum
NRO deko3d de gameplay foi gerado. Não há ganho FPS medido nesta etapa.

## Ligação em execução e módulo nativo

`nx_deko_abi_directory` agora liga os SPIR-V adaptados ao renderer PC real,
ajusta o range do descriptor, a alocação e o conteúdo do upload. Os geometry
shaders auxiliares de pontos também usam o mapa novo. A opção é init-only e
fica desligada quando o diretório está vazio. Arquivo ausente não cai no
shader antigo com um upload incompatível.

Validação real: duas execuções PC de 150 s, mesmo EXE e mesmos hashes de DLLs,
chegaram à fase e encerraram com código 0. A variante adaptada carregou 121
módulos, sem faltantes, e ambas chegaram ao frame 3900, com 29 screenshots.
O usuário também confirmou a imagem. Capturas finais inspecionadas mostram
personagem, HUD e cenário; as animações estão em instantes diferentes, portanto
não há prova de igualdade byte a byte. Isso continua sendo **Vulkan no PC**.
Evidências em `outputs/deko-runtime-pc/comparison.json`.

O módulo `deko_bindings.cpp` já compila em AArch64 e liga com as bibliotecas
reais `libdeko3d` e `libnx`. Usa o Nucleo do SDK no commit documentado abaixo:

- carrega DKSH retendo sua seção de controle e valida cabeçalho/tamanho;
- reempacota SystemConstants na memória de upload do recorder;
- liga UBOs 0–4, SSBO de memória bruta e texturas por estágio;
- grava comandos de desenho, shaders, targets, rasterização, MSAA,
  viewport/scissor, blend, profundidade/stencil e índices;
- usa invalidação de código e tabelas de descritores do Nucleo.

`build_native_bindings.ps1` reproduz a compilação no Docker já instalado.
Artefatos: `outputs/deko-native/libgreen_lantern_deko_bindings.a` e
`link-check.elf`. O ELF serve somente para confirmar símbolos e bibliotecas,
não é um jogo, não foi executado no console e não deve ser empacotado em NRO.

**Ligação ainda pendente no jogo:** o leitor PM4 atual não chama `RecordDraw`.
O módulo recebe um draw já preparado; não interpreta registradores, não cria
texturas a partir do guest, não implementa a propriedade EDRAM/aliases,
resolves/clears nem o presenter. Esses componentes ainda usam Vulkan no jogo.
Assim, a biblioteca compilada não significa que o backend completo esteja
integrado ou que um NRO deko3d esteja pronto para teste.

## Regenerar o contrato de upload

`generate_upload_contract.py constant-moves.jsonl system_upload_layout.h`
gera o packer e o mapa de offsets compartilhado pelos shaders auxiliares.
`--check` verifica o arquivo atual sem alterá-lo. O gerador exige um layout
SystemConstants igual entre variantes e recusa qualquer outro buffer que
precise mudar sem ter um packer correspondente no runtime.

## Imagens e saída nativas: implementação ainda sem frontend do jogo

`deko_images.cpp` implementa imagens 2D, upload de pixels lineares já decodificados,
cópias de cor com formato/tamanho compatíveis, clear de cor e depth/stencil,
descritores de imagem/sampler e aposentadoria dos recursos. Os formatos depth
são conferidos antes da criação. O workaround de tile size do SDK é aplicado.

O orçamento conta imagens vivas e aposentadas. O frontend deve chamar
`Images::BeginWork(n)` imediatamente depois de `Nucleo::EmpezarTrabajo(n)`.
O crédito de memória e dos slots de imagem só volta quando o slot que reteve
aquele recurso é reutilizado, depois da espera pelo fence feita pelo Nucleo.
O orçamento limita os payloads arrendados; o heap do SDK também contém folga
dos slabs, código, command buffers e uploads, que exigem contabilidade separada.

Não há implementação de untiling do guest, mipmaps/cubemaps ou resolves MSAA
nessa camada. `UploadLinear` requer pixels já decodificados. Depth/stencil↔cor,
cópias com formatos diferentes e MSAA retornam `NeedsConversion`; nenhum deles
é aproximado silenciosamente como uma cópia RGBA.

`deko_output.cpp` implementa uma saída direta por `Presentacion` do SDK:
swapchain, quad de saída, letterbox e tabela gamma de 256 entradas. Usa um
recorder próprio de três slots, descriptors por slot e shaders próprios
`output.vert.glsl`/`output.frag.glsl`. Os dois compilaram com uam no PC.
O produtor deve submeter o trabalho antes de chamar Present e manter a imagem
viva até a aposentadoria ordenada na mesma fila. Inicialização/apresentação
devem ocorrer no thread dono do NWindow. Orientação e resultado visual dessa
saída deko ainda não foram testados em hardware.

Todos os componentes compilam e ligam no ELF de link-check com libnx/deko3d.
**Isso não liga o backend ao leitor PM4 existente.** Falta preparar DrawPackets
a partir dos registradores do Lanterna, decodificar/uploadar texturas do guest,
implementar conversões EDRAM/aliases e resolves, shaders auxiliares de
geometria/pontos, e substituir provider/presenter no graphics system. O jogo
continua executando pelo Vulkan; ainda não foi gerado um NRO deko3d de gameplay.

## Adaptação validada

Executar com `--adapt-constants` depois dos dois diretórios ativa a adaptação.
`constant_layout.h` gera um único plano para reescrever offsets/strides SPIR-V
e reempacotar os bytes do upload (`BufferLayout::Pack`). Os índices dos campos
e as operações do shader são preservados. Tipos ainda não implementados
(matrizes, estruturas aninhadas, arrays multidimensionais ou não literais)
são recusados explicitamente.

`XeSystemConstants` passa de 504 para 528 bytes em std140. Os campos adicionais
que estavam nos offsets 496/500 passam para 128/132 na ordem da declaração.
Os demais blocos desta captura mantêm seus tamanhos. `constant-moves.jsonl`
registra os movimentos por shader/set/binding; o backend deverá consumir
exatamente esse contrato, incluindo tamanho, alinhamento e binding.

Validação CPU: 479 casos de blocos, cada um com três padrões de bytes.
Os valores são lidos pelos offsets e strides refletidos independentemente no
SPIR-V original e no adaptado, em vez de usar o próprio mapa como referência.
Uma corrupção proposital no upload precisa ser detectada em cada caso.
O preenchimento do padding de destino é zero.

Seis pixel shaders também precisaram de uma adaptação de descarte:
`deferred_discard.h` mantém a execução após `demote` e descarta o fragmento no
retorno do entry point. A transformação é restrita a fragment shaders sem
chamadas de função, queries de helper, subgrupos, atomics, image writes ou
stores com destino externo/desconhecido. Armazena o indicador em Private e
inclui a variável na interface exigida por SPIR-V 1.4+. Esta restrição evita
suprimir efeitos observáveis; equivalência de pixels ainda exige teste gráfico.

Resultado com o `uam.exe` instalado no PC: **122/122 GLSL aceitos e 122/122
DKSH gerados**, com cabeçalho DKSH e SHA-256 por módulo registrados em
`outputs/deko-preflight/adapted-final/uam-results.json`. Seis usam descarte
adiado. Não houve warnings do conversor nesse corpus. `compile_corpus.py`
reproduz a compilação e retorna erro se algum módulo falhar.

Nenhum desses módulos substitui ainda o shader Vulkan em execução. Constantes,
texturas, fetches e recursos do novo backend precisam ser ligados ao mesmo ABI.
Não interpretar compilação bem-sucedida como teste visual, compatibilidade de
todos os shaders do jogo ou previsão de FPS no console.

O executável `deko_preflight` compila o conversor real `rex::deko3d::SpirvAGlsl`
do SDK nx-mod, junto com SPIRV-Cross. Recebe os SPIR-V produzidos pelo renderer
Vulkan atual e registra a conversão e o layout das UBOs. Não desenha em deko3d,
não compila DKSH e não mede desempenho Tegra.

## Fontes

- SDK: `nx-mod/rexglue-sdk-nx`, commit
  `f961303fe3c6e3a90905b379ff4bf3de9a35a757`, snapshot local
  `work/deko3d-sdk-reference`. Licença BSD-3-Clause na raiz; preservar os avisos.
- SPIRV-Cross: revisão `22b22f56` citada no guia USF4, snapshot local
  `work/deko-spirv-cross`, licença Apache-2.0. Os fontes de terceiros são
  referenciados pelo CMake, não copiados para este diretório.

## Compilar e executar

Configurar este diretório com CMake e informar `DEKO_REFERENCE` e
`SPIRV_CROSS_REFERENCE` como caminhos absolutos desses snapshots. Compilar
`deko_preflight` em Release, com um processo. Uso:

```
deko_preflight diretorio-dos-spv diretorio-do-relatorio
```

Saída `conversion.jsonl`: resultado/erro por módulo. `interfaces.jsonl`:
nome, descriptor set, binding, tamanho e offsets dos membros das UBOs.
GLSL é gravado somente quando o conversor aceita o módulo. Exit 1 significa
rejeição de um ou mais shaders; 2 uso incorreto; 3 entrada/reflexão inválida;
4 nenhum módulo encontrado. Aceitar GLSL não comprova compilação uam, ABI,
especializações corretas ou equivalência visual.

A flag existente `nx_dump_shaders_path` agora também grava microcódigo host-endian
(`.txt.ucode`) e cada variante traduzida (`.spv`, hash e modification no nome).
Capturas são locais, derivadas da cópia do usuário, e não devem entrar no GitHub.
O caminho padrão vazio deixa a captura desligada. Async desligado foi usado
no teste para obter a sequência estável do replay.

## Resultado inicial no PC, antes da adaptação

- Build do jogo e da sonda: sucesso.
- Replay menu→fase por 150 segundos: saída 0; captura final inspecionada mostra
  personagem, cenário e HUD. Esse desenho continua sendo produzido pelo Vulkan.
- Conversor: controle mínimo de vertex shader aceito, com GLSL válido sintaticamente
  para sua saída simples; compilação uam ainda não testada.
- Shaders do jogo: **122 variantes examinadas, 122 rejeitadas** pelo conversor
  atual por layout de buffer não representável como std430/std140/scalar.
- Exemplo `XeSystemConstants`: 504 bytes, offsets não monotônicos incluindo
  496/500 entre campos em 28 e124. Os offsets explícitos do nosso tradutor
  Vulkan não correspondem à biblioteca HLSL preparada para deko desse SDK.

Evidências: `outputs/deko-preflight/conversion`, controle em `fixture-result`,
execução em `outputs/native-pc-user/deko-shader-preflight/case.json` e `game.log`.
SHA-256 do executável rodado:
`E1E515C4C45B4704C0FEA9808BDCC1CCBA8D782D8522BA56B0A79CFFA2D64B83`.

## Integração ainda necessária

O ABI de constantes já tem plano e packer offline. Integrá-lo aos uploads reais
e mapear texturas/fetches/recursos do Lanterna para o renderer deko. Não trocar
somente os shaders sem aplicar os respectivos planos de upload.

Após a compilação offline completa, integrar o adapter PM4/RenderDk, incluindo clears, resolves, aliases, stencil e
apresentação. Comparação visual no PC exige um caminho host com o mesmo ABI.
A execução nativa deko3d e seu FPS continuarão exigindo o Switch.
