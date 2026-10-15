> Histórico da revisão 035. Para o bloqueio atual, consulte [PORT_STATUS.md](PORT_STATUS.md).

# Evidências do console e versão atual para teste — 14/10/2026

## Bloqueio atual

O log 035 inicializa o NVK no Tegra X1, cria uma swapchain de 1280 × 720 e registra:
`Renderer ready: 160 MB stream, 1 shared memory binding(s)`.
Em seguida, as duas `XHostThread` da GPU nativa falham em
`pthread_create(stack_size=524288)`, com `errno` 12.
A tabela de funções inicializa depois disso, mas o log não demonstra o processamento da fila de
comandos do jogo, a apresentação de imagens ou a renderização de um quadro.

Em cada falha, consultas bem-sucedidas ao Horizon informam **13 de 96 threads** e
**1281 de 3189 MiB** de recursos de memória do processo. Esses contadores mostram que os dois
limites consultados ainda não tinham sido atingidos naquele instante. Eles não medem o maior
bloco disponível para alocação na área fixa de memória do `malloc`.

O log 033 usou pilhas de 2 MiB para as threads auxiliares, e ambas falharam. O log 034 usou 1 MiB
e registrou falha apenas para a thread da fila de comandos. No log 035, com 512 KiB, as duas
voltaram a falhar. Diminuir as pilhas não resolveu a inicialização de forma consistente; a ausência
de uma linha de erro no log 034 não comprova que houve renderização.

Foi inspecionado o binário do libnx vinculado ao executável: `__syscall_thread_create` retorna
`ENOMEM` quando falham `threadCreate`, `svcSetThreadCoreMask`, `threadStart` ou a alocação dos
metadados do pthread. Portanto, o `errno` sozinho não identifica qual operação falhou.
`threadCreate` também aloca memória alinhada por página para pilha/TLS/reent e a mapeia com
`svcMapMemory`. O TLS do executável anterior tinha 0x540 bytes; seu tamanho não era uma grande
alocação oculta por thread.

## Correção candidata atual: ainda sem validação no console

- Reserva dois blocos de 1088 KiB, alinhados por página, para as pilhas antes da inicialização do
  NVK. Só as threads nativas de VSync e da fila de comandos usam esses blocos. As threads que
  executam o código recompilado do jogo continuam com pilhas do sistema hospedeiro de 2 MiB.
- O libnx continua responsável por mapear as pilhas, criar as threads e cuidar de TLS, prioridades
  e espera pelo encerramento. A reserva fornece a memória de suporte; cada bloco só fica disponível
  novamente após um `threadClose` bem-sucedido.
- Registra os códigos `Result` originais das operações de mapeamento de pilha, criação da thread
  no kernel, definição da máscara de núcleos e início da execução. As falhas de mapeamento também
  registram tipo, atributos e permissões da memória de origem.
- Verifica o resultado de `Create()` das duas threads gráficas. Antes, esses resultados eram
  ignorados, e a configuração da GPU retornava sucesso mesmo sem as threads. Agora a falha é
  propagada; se a criação da thread da fila falhar, a thread de VSync já iniciada é encerrada.
- Registra os valores de memória usada, livre e do bloco superior do `malloc`, junto dos contadores
  de recursos do Horizon que já existiam.

A causa da falha do log 035 **ainda não foi identificada**. A reserva antecipada trata uma hipótese
relacionada à alocação alinhada ou às páginas de origem; ela não comprova essa hipótese. A versão
altera a forma e a ordem da alocação e acrescenta informações de erro, mas ainda não é uma correção
confirmada no console.

NRO: `green_lantern_native_renderer_reserved_threads.nro`, com 53.356.741 bytes.
SHA-256: `536ED663A17562F5990C53259F8BAF8658B112997B6657BED26C850B1958E15A`.
A compilação para Switch e a geração do pacote terminaram com sucesso. Foi verificado que o
executável final chama os cinco interceptadores de diagnóstico. Os NROs anteriores estão
preservados localmente. O NRO contém código gerado do jogo e não está neste repositório.

## Como interpretar a próxima execução

- `Both GPU worker threads created successfully`: este bloqueio de inicialização foi ultrapassado.
  Procurar em seguida a fila de comandos do jogo, as apresentações de imagem e a renderização.
  Criar as threads, por si só, não comprova a exibição de um quadro do jogo.
- `stack map raw failure`: examinar o `Result` original do kernel e os metadados da página de origem.
- `threadCreate raw failure`, sem uma falha de mapeamento ou criação no kernel: examinar a alocação,
  a validação dos argumentos e as estatísticas do `malloc`. O uso da reserva é registrado no log.
- `thread core-mask raw failure` ou `threadStart raw failure`: investigar a operação indicada e as
  capacidades do processo. O `errno` 12 não deve ser tratado como uma medição da RAM disponível.

## Caminho anterior: renderizador Xenos

Uma arena de 256 MiB, reservada cedo para a memória de suporte do Xbox 360, ultrapassou a falha
original de alocação alinhada durante um pedido de 35 MiB do jogo. Depois, faltou espaço para um
bloco de 16 MiB, quando restavam apenas 12 MiB livres. Com uma arena de 384 MiB, o NVK ficou sem
memória antes disso, ao criar um alvo de profundidade. Foi compilada uma versão com arena de
320 MiB como valor intermediário; seu NRO original com Xenos ainda não foi testado no console.
O experimento atual com renderizador nativo também usa a arena de 320 MiB.

Área fixa do `malloc`: 1280 MiB. Limite experimental do STREAMDATA: 32 MiB.
Buffer de fluxo de dados do renderizador nativo: 160 MiB.
O buffer compartilhado de 512 MiB do Xenos, alocado integralmente quando não há residência esparsa,
explica grande parte do consumo gráfico do caminho antigo. O experimento nativo usa outro
renderizador; seu orçamento de memória precisa ser avaliado separadamente.

## Perguntas para o desenvolvedor do Project8

1. Seu port encontrou falhas de alocação ou mapeamento das pilhas do libnx depois de inicializar
   o NVK? Fornecer memória reservada antecipadamente foi suficiente ou foi preciso tratar os
   atributos das páginas da heap e o posicionamento dos espelhos das pilhas?
2. Como você divide o orçamento entre a heap do libnx, a memória de suporte do Xbox 360 e as
   alocações do NVK acessíveis pela CPU no modo de aplicação completa? Você mantém uma reserva
   para as alocações do próprio runtime, além dos blocos de memória do jogo?
3. Existem diferenças de versão do SDK que afetem as chamadas de máscara de núcleos ou de início
   das threads do libnx, ou o registro de MMIO do jogo antes de criar as threads gráficas?
   Os novos diagnósticos devem permitir distinguir esses pontos de falha.

Os testes no console usam o modo de aplicação completa: segurar R ao abrir um jogo para entrar
no homebrew. Eles não são executados pelo Álbum em modo applet.
