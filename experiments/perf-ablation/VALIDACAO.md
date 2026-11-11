# Verificação local — 30/10/2026

Revisão de 05/11/2026: os smoke tests PC abaixo usaram defaults antigos do
launcher (`GuestDepth=false`, `VertexSafety=0`). Eles verificam execução e
contadores, mas não são prova de correção visual equivalente ao Switch. Os
testes corretos posteriores em `outputs/internal-scale/` passam explicitamente
`GuestDepth=true` e `VertexSafety=8`. O main do NRO do Switch já aplicava as
opções corretas; essa falha do launcher PC não muda as medições hardware 088/089.

- Build Switch concluído, quatro fontes nativos e main recompilados em pasta
  privada. NRO: 54.646.981 bytes, SHA-256
  `f3f8cc40be55fe696ddd2a7d988a03635d72283e511a21f30b7ecfcb05bf910e`.
- Build PC privado concluído. Quatro execuções sequenciais do mesmo EXE/DLLs:
  baseline e scissor por 65 segundos; sem draws e sem resolves por 45 segundos.
  Todas encerraram com código zero e sem os marcadores `VK_ERROR_DEVICE_LOST`
  ou `vkQueueSubmit failed`. Não é prova de ausência de qualquer erro Vulkan.
- Baseline e scissor chegaram à fase com replay. Inspeção das capturas:
  baseline com imagem do jogo; scissor com a metade direita ausente, conforme
  intervenção. Sem draws produziu imagem preta; sem resolves não produziu
  captura apresentada. Este último registrou zero resolves e tempo dessa seção
  igual a zero. FPS nesses modos inválidos não é FPS jogável.
- O parser verificou em todas as janelas emitidas que a soma dos quatro grupos
  de intervalos é o número de quadros e que FPS = quadros / duração medida,
  dentro do arredondamento do log. Resultados nos `pc-*-analysis.json`.
- Médias do PC estão limitadas pelo pacing e misturam loading/menu/gameplay.
  Não foram usadas para estimar ganho no Switch ou promover otimização.
- Clocks não podem ser validados por execução Windows. No Switch, registrar
  código de erro por leitura evita tratar indisponibilidade como clock zero.
- Manifesto de fontes, executável PC, argumentos, rollback e NRO em
  `validation.json`. Receitas e logs de compilação preservados nesta pasta.

As ablações preservam a estrutura de preparação, mas alteram imagens e podem
alterar caches e o caminho seguido pelo jogo ao longo do tempo. Uma comparação
válida pede cena semelhante, não apenas médias de sessões distintas.

Ainda não houve teste deste NRO no hardware. Primeiro teste: `ablation_mode=0`
e `partial_submit_draws=0`; instruções em `LEIA-ME.md`.
