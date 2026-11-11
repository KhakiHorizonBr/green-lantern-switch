# Execução 088 — diagnóstico Vulkan, 30/10/2026

## Conclusão

As leituras de clocks e o contador por duração medida funcionaram. Há trechos rápidos e lentos; não inferir um FPS típico de gameplay sem identificar as cenas. Os novos contadores não demonstraram erro aritmético: cada janela passou na checagem quadros/segundos. Isto não valida independentemente o scanout do painel.

Modo de ablação 0 e submissão parcial 0 confirmados. CPU 1020 MHz e EMC 1331,2 MHz em todas as leituras; GPU inicialmente 307,2 MHz, depois 460,8 MHz nas demais. Modo de operação/desempenho 0 (portátil). Result de todas as leituras zero. As taxas são informadas pelo serviço, não utilização contínua.

## Trechos observados

| Final da janela no log | Segundos | Swaps | FPS medido |
| --- | ---: | ---: | ---: |
| 2026-10-30 00:58:25.182 | 10.003 | 294 | 29.391 |
| 2026-10-30 00:58:35.205 | 10.023 | 299 | 29.831 |
| 2026-10-30 00:58:45.247 | 10.042 | 124 | 12.349 |
| 2026-10-30 00:58:55.311 | 10.064 | 127 | 12.619 |
| 2026-10-30 00:59:05.378 | 10.067 | 127 | 12.616 |
| 2026-10-30 00:59:15.404 | 10.026 | 122 | 12.169 |
| 2026-10-30 00:59:25.438 | 10.034 | 160 | 15.946 |
| 2026-10-30 00:59:35.445 | 10.007 | 245 | 24.482 |
| 2026-10-30 00:59:45.464 | 10.020 | 245 | 24.452 |
| 2026-10-30 00:59:55.494 | 10.030 | 192 | 19.143 |
| 2026-10-30 01:00:05.577 | 10.083 | 85 | 8.430 |
| 2026-10-30 01:00:15.673 | 10.096 | 86 | 8.518 |
| 2026-10-30 01:00:25.748 | 10.074 | 79 | 7.842 |
| 2026-10-30 01:00:35.833 | 10.085 | 78 | 7.734 |
| 2026-10-30 01:00:45.892 | 10.059 | 77 | 7.655 |
| 2026-10-30 01:00:56.054 | 10.162 | 73 | 7.184 |
| 2026-10-30 01:01:06.073 | 10.020 | 81 | 8.084 |
| 2026-10-30 01:01:16.192 | 10.118 | 85 | 8.400 |
| 2026-10-30 01:01:26.253 | 10.061 | 84 | 8.349 |
| 2026-10-30 01:01:36.284 | 10.031 | 85 | 8.474 |
| 2026-10-30 01:01:46.287 | 10.003 | 85 | 8.497 |
| 2026-10-30 01:01:56.331 | 10.044 | 82 | 8.164 |
| 2026-10-30 01:02:06.344 | 10.013 | 79 | 7.890 |
| 2026-10-30 01:02:16.379 | 10.035 | 64 | 6.378 |
| 2026-10-30 01:02:27.287 | 10.908 | 32 | 2.934 |
| 2026-10-30 01:02:37.356 | 10.069 | 71 | 7.051 |
| 2026-10-30 01:02:47.393 | 10.037 | 102 | 10.163 |

Os intervalos de 00:59:25 a 00:59:45 ficam perto de 24,45–24,48 FPS. A partir da janela encerrada em 01:00:05, muitas janelas consecutivas ficam em 7–9 FPS, até cerca de 01:02:06; portanto não são apenas um único quadro ruim nesta execução. Não sabemos quais cenas o usuário estava vendo nesses instantes. A janela de 2,93 FPS posterior pode incluir uma transição/pausa; o log não permite atribuição segura.

As janelas curtas cobrem 286,948 s, 12,609 conclusões/s no agregado, misturando intro/menu/loading/gameplay. Esse agregado não será usado como FPS de gameplay. São 173,419 s em janelas abaixo de 10; 49,995 s entre 10 e 20; 31,936 s entre 20 e 25; 31,598 s a partir de 25. São faixas de médias de janelas, não faixas de cada intervalo individual.

## Custo e próximo teste

A amostra GPU final por passe informa 67,3 ms, com 30,51 ms no principal passe c=t1328p1280f d=t0p1280f. Uma amostra anterior informa 73,1 ms, com 33,36 ms nesse passe. Há custo relevante de rasterização/shading a investigar; isso não prova que a GPU explica toda a latência. Os tempos de CPU incluem esperas e não devem ser somados aos de GPU.

Usar o MESMO NRO de diagnóstico com `ablation_mode=2` e `partial_submit_draws=0`, mantendo console/modo/clocks e demais configurações. Repetir trecho semelhante por um a dois minutos. A metade direita pode ficar ausente ou conter conteúdo inválido: a intervenção corta a largura de todos os scissors pela metade, não é uma escala de resolução pronta. Não interpretar FPS nesse modo como ganho jogável.

Se tempo GPU cair substancialmente na mesma cena e com clocks semelhantes, priorizar escala real de resolução/carga de fragmentos. Se pouco mudar, avaliar a ablação de draws e CPU/preparação/esperas. Um resultado nulo de scissor não elimina custos de banda ou cópias que ele mantém.

Sem build novo nesta rodada. O NRO anterior e o baseline permanecem preservados. Dados e instruções são locais; esta rodada ainda não foi publicada no GitHub.
