# Hardware 092 — escala interna 75%, 05/11/2026

## Decisão

A variante de conversão em cada scope não demonstrou ganho e fica rejeitada
como otimização para uso normal. Voltar a `internal_scale_percent=100` ou usar o
NRO de diagnóstico anterior. Não pedir repetição imediata do mesmo experimento.

O hardware 089 sustentava a hipótese de custo sensível a pixels. O 092 não
derruba essa hipótese; mostra que esta implementação de compatibilidade tem
custo adicional importante. Uma futura versão precisa reduzir conversões de
tamanho, preservando amostragem coerente entre prepass, material e stencil.

## Aplicação confirmada

- `[gl-internal-scale-v1] percent=75 applied=true`.
- `ablation_mode=0`, submissões parciais zero.
- Superfícies menores efetivamente criadas; dez superfícies temporárias no
  último contador. Último checkpoint: 10412 scopes e 59934 blits acumulados.
- Clocks informados pelo serviço: CPU 1020 MHz, GPU 460,8 MHz após a leitura
  inicial, EMC 1331,2 MHz, modo portátil. Não há evidência de ganho por clocks.
- Sem os marcadores de falha/capacidade verificados em `profile.json`.
  Isso não significa ausência de todos os erros possíveis; o log não tem
  encerramento normal na cauda fornecida.

## Tempos e ressalvas

Há uma sequência de janelas em cerca de 3,52–3,53 conclusões/s, mesmo sem
incremento nos contadores de shaders/pipelines (55/58). Esse trecho não pode
ser explicado apenas por compilação inicial. Mais adiante há janelas próximas
de 12,2–12,5, seguidas de 3,41/2,53/2,99 em uma transição para muito mais draws.
Nos últimos trechos os contadores de shaders e pipelines ainda crescem; não
há uma janela longa claramente estabilizada de gameplay pesado no fim.

Amostras de GPU total por passe: 22,1 ms em trecho leve, 131,1 ms em um trecho
posterior e 49,8 ms em outro trecho. Na amostra de 131,1 ms, o segmento
depth-only responde por 60,25 ms. Esse segmento inclui conversões do protótipo;
não interpretar como custo puro do teste ou shader de profundidade. Apenas
11 draws desse passe aparecem na amostra, sinal compatível com custo alto
por scope/transferência, não só custo de sombrear muitos triângulos.

No PC a mesma versão já aumentava a última amostra total de 7,4 para 9,5 ms
com cenas próximas. A inspeção de código confirma conversões de cor/depth/
stencil na entrada e saída de cada scope reduzido. Em conjunto com os
contadores do Switch, isso torna o custo de conversão o principal suspeito da
regressão. O log atual não cronometra cada blit isoladamente; não atribuir um
percentual exato nem chamar todas as esperas de CPU ativa.

As janelas curtas emitidas somam 167,223 s e média agregada de 7,403 conclusões/s,
incluindo intro/menu/loading/fase. Essa média não é FPS típico da fase e não é
comparação pareada com o 088/089. Não há medição de scanout do painel.

## Próximo foco técnico

1. Manter superfícies reduzidas válidas entre scopes que compartilham dados.
2. Converter de volta somente quando um resolve, alias EDRAM, operação nativa
   ou captura realmente consumir a representação inteira.
3. Rastrear explicitamente qual representação foi alterada, incluindo clears,
   restauração de stencil, crescimento de alvos e retiradas após fences.
4. Medir separadamente conversão e desenho, e validar no PC a sequência de
   prepass/material/stencil antes de pedir outro teste de hardware.

Isso é alteração estrutural de coerência, não ajuste de heap ou simples troca
para 50%. Não pedir 50% desta versão: as conversões continuam presentes e o
resultado não ataca a causa identificada. O baseline e os artefatos anteriores
permanecem preservados. Nenhum NRO novo nem publicação GitHub nesta análise.
