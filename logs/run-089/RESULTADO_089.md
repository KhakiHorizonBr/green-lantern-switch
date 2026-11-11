# Execução 089 — ablação de scissor, 30/10/2026

## Resultado

`ablation_mode=2 applied=true` confirmado; submissões parciais permanecem em
zero. CPU 1020 MHz, GPU 460,8 MHz após a leitura inicial de 307,2 MHz, EMC
1331,2 MHz. Modo portátil. As leituras retornaram sucesso, como no 088.

Há redução relevante dos tempos de GPU com metade da largura do scissor.
A intervenção mantém a resolução/alocações dos alvos e as rotinas de resolve;
não é uma otimização jogável, porque corta a imagem e altera o conteúdo dos
passes seguintes.

| Amostra | 088, normal | 089, scissor metade |
| --- | ---: | ---: |
| Horário no log | 01:02:05.732 | 01:10:55.993 |
| Draws do passe principal | 442 | 445 |
| GPU total amostrado por quadro | 73,1 ms | 54,0 ms |
| Passe principal | 33,36 ms | 21,63 ms |

Nessas amostras, redução aproximada de 26% no total e 35% no passe principal.
Não são o mesmo quadro ou replay determinístico, e os demais passes diferem;
portanto não tratar os percentuais como efeito causal exato. Os clocks iguais,
o volume próximo de draws e a consistência de outras amostras sustentam a
hipótese de custo importante sensível a fragmentos/pixels.

Nas amostras com pelo menos 250 draws no passe principal: 088 tem GPU total
67,3–77,4 ms e passe principal 30,51–34,98 ms; 089 tem total 44,0–54,0 ms e
principal 17,82–21,63 ms. O limiar não identifica uma cena; serve somente para
evitar misturar amostras leves de menu com fases mais carregadas.

## FPS e limites da comparação

Janelas longas com pelo menos 700 draws preparados por quadro:

- 088: 16 janelas, 2,93–8,52 FPS; agregado ponderado por duração 7,57.
- 089: 9 janelas, 9,30–12,28 FPS; agregado ponderado por duração 10,71.

O 088 inclui uma janela com pausa muito longa. Excluindo-a, a comparação muda;
não anunciar ganho percentual de FPS entre sessões. Os replays/caminhos e a
duração são diferentes, e os critérios não distinguem combate, cutscene ou
transição. Os contadores medem conclusões de swap observadas pela CPU, não
scanout do painel. No 089 inteiro há 154,165 segundos em janelas curtas e média
agregada 15,853 conclusões/s, incluindo cenas leves e carregamento: não é FPS
típico do gameplay.

Mesmo com o scissor reduzido, a GPU continua acima dos 33,3 ms desejados em
amostras pesadas. Preparação de vértices, gravação e esperas também permanecem
custosas. Os tempos por seção são wall time; não classificam execução ativa
de CPU versus driver, nem devem ser somados aos de GPU.

## Decisão

O teste dá justificativa para implementar e validar escala interna real,
preservando o quadro inteiro, ao invés de apenas trocar a resolução da saída.
Isso exige coerência entre tamanhos dos alvos, viewport/scissor, resolves,
restauração de profundidade/stencil e coordenadas de amostragem de alvos.
Validar a correção no PC antes de entregar outra variante ao Switch.

Não promover o modo 2 como renderer normal. Para voltar à imagem completa no
NRO atual, usar `ablation_mode=0`. Os modos de imagem vazia 1/3 não foram
pedidos nesta rodada: já há sinal suficiente para priorizar a carga de pixels,
sem impor imediatamente outro teste de navegação sem imagem ao usuário.

Sem build novo ou publicação GitHub nesta análise. Fontes compartilhados do
fork deko3d não foram alterados. Evidências em `analysis.json`,
`comparison-088-089.json` e no log copiado nesta pasta.
