# Escala interna seletiva — protótipo isolado

## Objetivo

O hardware 089 mostrou custo de GPU sensível ao número de pixels: o scissor
pela metade reduziu o tempo do passe principal. Esta variante mantém a imagem
inteira, reduzindo as duas dimensões do raster dos alvos principais conhecidos.
O ganho de desempenho só poderá ser estabelecido no Switch.

## Estratégia e limites

- `internal_scale_percent=100` é o padrão, sem superfícies menores.
- `75` desenha o alvo principal de 1280x720 em 960x540 (56,25% dos pixels).
- `50` desenha em 640x360 (25% dos pixels), opção mais agressiva.
- São elegíveis scopes com cor 0 no tile 1328/pitch 1280 ou com a profundidade
  principal no tile 0/pitch 1280, com altura pelo menos 480 e sem MSAA.
  Isso inclui o prepass de profundidade: todos precisam usar as mesmas posições
  de amostragem. Outros passes continuam nativos.
- Alvos, formatos e endereços guest-facing permanecem no tamanho original.
  No início do scope elegível, cor/depth/stencil são reduzidos para attachments
  temporários; no final são ampliados de volta antes da publicação EDRAM e dos
  resolves. A primeira versão privilegia coerência, não economia de RAM.
- As conversões extras custam banda e GPU. A técnica pode não compensar em
  passes pequenos ou no Tegra; não prometer FPS antes de medir o total.
- Os blits usam NEAREST para cor, profundidade e stencil. Isso reduz definição
  e pode produzir serrilhado; não é antialiasing ou reconstrução temporal.
- Blits só cobrem a área comum aos attachments, preservando o restante das
  superfícies maiores. As dimensões de memória temporária podem ser maiores
  que a área desenhada, por exemplo depth 960x1536 para um guest 1280x2048.
- Cada formato precisa anunciar BLIT_SRC e BLIT_DST; caso contrário o scope
  continua nativo e registra aviso. Falha de criação também impede seu uso.
- Shaders de pixel usados em scopes menores têm variantes SPIR-V privadas:
  leituras de FragCoord X/Y são multiplicadas pelo inverso da escala para
  manter coordenadas guest. Z/W permanecem iguais. Padrões não reconhecidos
  impedem a escala daquele draw; não se altera o tradutor global do SDK.
- Viewport/scissor são escalados somente nos comandos enviados, preservando
  as coordenadas usadas no rastreio EDRAM, nos clears e nos resolves.

## Testes e interpretação

O launcher PC genérico tem defaults antigos. Para comparar com o Switch, passar
explicitamente `-GuestDepth true -VertexSafety 8`. As execuções sem esses dois
argumentos foram smoke tests e mostraram o defeito antigo do personagem escuro;
não são a referência de correção ou de performance desta variante.

Referências finais: `internal-scale-100-aligned` e `internal-scale-75-aligned`
na pasta `outputs/native-pc-user`, com os mesmos EXE/DLLs, replay e flags,
mudando a escala e a opção de dump dos shaders (somente em 75%, fora do trecho
de comparação final). A tentativa `75-correct` anterior tinha os flags corretos,
mas ainda misturava prepass nativo com material reduzido e foi rejeitada por
artefatos. Ver o relatório de validação ao
lado antes de considerar qualquer variante aprovada no PC/hardware.

`check-fragcoord.cmd` compila um shader sintético com leituras vetoriais e
escalares e verifica a transformação e rejeição de entradas truncadas. Asserts
estão ativos. Isso não substitui validar SPIR-V com ferramenta completa nem
comparar a imagem do jogo. Os módulos do jogo são criados no driver nos testes.

## Isolamento

Esta pasta parte dos fontes do diagnóstico 088/089. Não modifica os fontes
compartilhados com o fork deko3d. Build PC e Switch usam objetos guest e
bibliotecas já compilados, com fontes nativos privados. Não é um rebuild
autossuficiente de todo o SDK. Os scripts preservam manifestos de link.

Rollback: NRO anterior em `outputs/perf-ablation/green_lantern_diagnostico.nro`.
O novo NRO deve ficar em arquivo separado. A escala inicia em 100%; o usuário
escolhe 75% no tuning.ini para o experimento, com `ablation_mode=0`.

A opção 50% está presente no protótipo, mas não foi validada nesta rodada.
O teste solicitado usa somente 75%, com rollback a 100%.
