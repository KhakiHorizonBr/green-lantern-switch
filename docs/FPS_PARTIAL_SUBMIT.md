# Envio antecipado de comandos — variante de 28/10/2026

## Hipótese

No hardware074 há esperas de 59–69 ms por espaço no stream. Enviar trabalho
antes de preencher o stream pode fazer a GPU consumir seus dados enquanto a
CPU prepara o restante do quadro. A inspiração é o patch0011 de Dead Space2,
documentado em PORTS_X360_AUDIT_2026-10-28.md. A implementação usa nosso próprio
`Submit`/`BeginRecording`, sem importar o renderer deko3d.

`nx_partial_submit_draws=0` mantém o comportamento anterior. Um valor positivo
envia o lote antes do próximo draw, após registrar N draws no lote atual. A
contagem reinicia também quando uma alocação força o envio por falta de espaço.
Draws ignorados não contam. O ponto escolhido antecede a preparação dos dados
do próximo draw; o recorder é drenado, o render pass encerrado e os caches de
bindings reiniciados pelo caminho existente. Não remove barreiras Vulkan.

No Switch, `partial_submit_draws=256` em
`sdmc:/switch/green_lantern/tuning.ini` ativa a variante no mesmo NRO.
`partial_submit_draws=0` desliga; a ausência da chave também desliga.
Heap, arena, stream, resolução, shaders, controles e limite de30FPS permanecem
como antes. Não reescrever o tuning.ini inteiro: preservar as outras chaves.

## Evidência e critérios

O log `[gl-partial-submit]` informa limite, quantidade acumulada de envios e
tempo wall acumulado do procedimento. Esse tempo inclui drenagem, uploads e
esperas por slots; não representa CPU ativa pura. Com timeline ligada, cada
envio periódico gera `partial_submit` com contagem de draws em `value`.

Validação PC: mesmo executável, mesmo replay de150s, capturas no quadro3000,
verificação byte a byte dos buffers de vértices esparsos contra a fonte usada
naquela execução e imagens de menu/fase. O replay por tempo não garante a
mesma pose da animação ou o mesmo estado de jogo entre execuções; não declarar
igualdade de todos os pixels com base nele. Análise de timing considera
quadros >= 3050 e pelo menos 400 draws, depois da captura do quadro 3000;
custos de captura e profiler existem.

Resultados possíveis no hardware:

- Menos espera de stream e menor p95, imagem correta: candidato útil.
- Mesma espera ou pior p95: envios extras podem ter consumido slots ou somente
  antecipado a espera; voltar a0. Mais envios não são automaticamente melhores.
- Artefato visual/device lost: rejeitar a variante e conservar o rollback.
- CPU melhora mas GPU continua acima de33,3ms: ainda falta otimizar carga GPU;
  não aumentar a heap como resposta automática.

Rollback preservado em `outputs/partial-submit/rollback-metricas.nro`; fontes
anteriores na mesma pasta. Nenhum ganho de FPS Switch foi medido nesta etapa.

## Isolamento entre os chats

O usuário confirmou que um fork trabalha em deko3d ao mesmo tempo. A primeira
comparação foi invalidada: os hashes dos executáveis divergiram entre as duas
rodadas. Os dados foram conservados, mas não podem sustentar uma conclusão de
desempenho. A repetição usa `outputs/partial-submit/isolated-bin`, com EXE e DLLs
copiados e hashes registrados. Um snapshot dos fontes desta variante também
fica separado; alterações de dump de shaders do outro chat foram preservadas.

## Resultado da repetição isolada

Mesmo EXE SHA-256 `E1E515C4C45B4704C0FEA9808BDCC1CCBA8D782D8522BA56B0A79CFFA2D64B83`,
mesmas duas DLLs, hashes confirmados novamente ao final. Duas execuções de
150 segundos chegaram à fase e encerraram com código 0. Zero eventos perdidos,
zero linhas de quadro incompletas. Dados em `outputs/partial-submit/comparison.json`.

| Medida PC, após captura | Padrão 0 | Variante 256 |
|---|---:|---:|
| Quadros selecionados | 737 | 768 |
| Intervalo p50 | 33,2964 ms | 33,2862 ms |
| Intervalo p95 | 34,3718 ms | 46,8745 ms |
| Intervalo p99 | 34,9312 ms | 48,2409 ms |
| Renderer wall p95 | 11,7097 ms | 13,4178 ms |
| Soma de durações GPU p50 | 7,8581 ms | 8,1938 ms |
| Soma de durações GPU p95 | 10,1172 ms | 11,0378 ms |

As durações GPU vêm das submissões de cada quadro totalmente observado;
não representam uma medição calibrada de ocupação de GPU, e somá-las ao wall
CPU seria incorreto. No PC a espera por fence de alocação continuou em zero.

O padrão gerou zero eventos `partial_submit`; a variante gerou 6107 na sessão
inteira, todos com 256 draws. As capturas de cada execução tiveram sete buffers
de vértices validados byte a byte contra sua própria fonte: 9.332.480 bytes no
padrão e 9.232.000 na variante. A quantidade/conteúdo não é idêntica entre as
sessões por serem replays por tempo, não replays determinísticos de comandos.
As imagens de menu e fase foram inspecionadas: sem regressão visual aparente.
Zero registros de `vkQueueSubmit failed`, `VK_ERROR_DEVICE_LOST` ou `[critical]`.

**Decisão: manter a variante desligada.** Não demonstrou ganho no PC e a
cadência piorou na comparação. Isso não prova o desempenho no Tegra, mas também
não justifica promover um NRO como otimização nem pedir teste de console agora.
O próximo candidato é geometria GPU residente com orçamento fixo e controle de
fences, para reduzir ocupação do stream em vez de apenas antecipar submissões.

PC e Switch compilaram. O link Switch precisou de `DEVKITPRO=C:/devkitPro`:
sem isso o specs procurava `/opt/devkitpro/libnx/switch.ld`. O NRO experimental
foi conservado desativado por padrão, SHA-256
`04E50E290FB623D2A3707A217056C5E0A73F75521D35947AA3B1DED5FAE5231A`.
O rollback anterior permanece intacto. Nada foi publicado no GitHub nesta etapa.
