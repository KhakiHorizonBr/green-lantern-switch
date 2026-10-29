# Tempos por quadro e linha do tempo — 27/10/2026

## Implementado

- Percentis exatos por nearest-rank: p50, p95, p99 e máximo em janelas de10s.
  Até8192amostras por distribuição, com contagem explícita de descartes.
- Intervalo de conclusão de swaps no CP, tempo wall do renderer, alocação,
  fences dentro da alocação, todas as fences e duração GPU das submissões concluídas.
- Draws: wall por chamada no ring, wall descontando as esperas monitoradas e
  wall por draw na thread que grava Vulkan. Não são tempos CPU do escalonador.
- vkUpdateDescriptorSets:8locais passaram por wrapper que conta chamadas,
  writes e tempo, preservando os argumentos Vulkan.
- Alocação: tempo total, fences e submissões forçadas por ocupação do stream.
  Isso evita atribuir toda a seção de vértices à cópia ou varredura.

Os contadores não mudam shaders, resolução, backing ou orçamento de stream.
A thread de gravação consome até32comandos disponíveis por iteração, mantendo
ordem e a publicação do tail por comando; custos completos são contados antes
de liberar o último comando do lote. Não usa mais threads do que já existiam.

## Linha do tempo opcional

nx_frame_timeline=false por padrão. No tuning.ini do Switch: frame_timeline=1.
Saída: sdmc:/switch/green_lantern/rex_timeline.jsonl. NoPC: logs/rex_timeline.jsonl;
--nx_frame_timeline_path permite outro destino.

Relógio único monotônico de CPU. Eventos: produtor notificando o ring (amostra
até200/s), gravação de comandos (um lote curto a cada32, mais lotes>=1ms),
submissão, espera em fence, alocação lenta, callback de apresentação abrangente,
intervalos de quadro e contadores por quadro. O produtor não tem fronteiras
completas de frame do guest: o doorbell é uma observação parcial, com frame
UINT32_MAX. Não representa o tempo total das threads do jogo.

fence_wait usa frame do CP que espera, submission esperada e value com o frame
que originou essa submissão. gpu_completion_observed marca quando a CPU percebe
conclusão e value traz a duração medida pelos timestamps GPU. NÃO é o instante
real de término GPU no relógio CPU nem um intervalo GPU calibrado nesse relógio.
O analisador soma durações GPU por frame apenas se observou todas as submissões
correspondentes concluídas. Não somar essas durações com spans CPU sobrepostos.

Buffer de eventos limitado a32768por flush (~1,75MiB; dois buffers durante flush).
Flush no relatório, mais encerramento normal. Falta de memória desliga timeline;
percentis registram descartes se não conseguirem amostras. Timeline desligada não
reserva esse buffer. Escrita de diagnóstico, logs e screenshots alteram timing:
não tratar estes testes como benchmark sem overhead. A saída pode perder o último
intervalo em encerramento forçado do processo; não preencher lacunas artificialmente.

## Analisador

outputs/frame-diagnostics/analyze-timeline.py recebe JSONL e gera summary.json e
timeline.chrome.json para importação local em visualizadores Chrome/Perfetto.
Não envia capturas para serviço externo. Aceita --start-frame, --end-frame e
--min-draws. O filtro de draws é heurística de carga, não detector real de cena.
Quadros com linhas incompletas são excluídos e reportados. Descarta a hipótese
de atribuir o tempo de uma fence antiga ao frame original como tempo de espera CPU.

## Comparação PC e decisão

Mesmo executável, mesmas flags/cópias de saves/replay, apenas nx_pace_before_present
alterado. Ambas execuções150s chegaram à fase e saíram com código0. Controles
reproduzidos por tempo de parede e cenas/caches podem variar; não são replay GPU
bit a bit. Filtro comum: frame>=2400 e pelo menos400draws, excluindo aquecimento.

| Configuração | Quadros | p50 | p95 | p99 | Máximo |
| --- | ---: | ---: | ---: | ---: | ---: |
| Espera após apresentar, padrão |1529|33,3815ms|34,2360ms|34,7653ms|48,4598ms|
| Espera antes de apresentar |1520|33,4112ms|35,4317ms|36,1354ms|36,8002ms|

Zero eventos descartados e zero linhas de quadro incompletas nas duas análises.
A primeira execução do medidor, antes da amostragem de lotes, saturou a timeline:
não usar sua aparente melhora48–50->35ms como prova. A repetição estabilizada
não demonstrou melhora consistente da variante. Padrão preservado após apresentar.
pace_before_present=1 deixa a variante disponível;0 restaura o padrão atual.
Não alterar o limitador nx_min_frame_vblanks=2 (30FPS em60Hz) nesta rodada.

No PC padrão, alocação p95=0,0748ms e espera de fence da alocação p99=0.
NoPC o custo do Switch não foi reproduzido: não concluir que as esperas observadas
no074 eram inofensivas, nem que a GPU Tegra tenha a mesma folga da RTX3060.
Duração GPU somada por frame completo: PC padrão p50=8,2231ms/p95=9,5167ms;
variante p50=7,4145ms/p95=16,3182ms. Não misturar percentil por submissão
(com1ou2submissões/frame) com custo total por quadro.

## Entrega e limites

Código e instrumentos prontos; compilação PC e Switch conferida, hardware desta
rodada não executado. NRO de métricas guardado em outputs/frame-diagnostics;
nenhuma promessa de ganhoFPS ou de35–40FPS. Usuário preferiu mais estudo antes
de outra rodada no console; não é solicitado teste de hardware nesta etapa.
Heap2000MiB, arena384MiB, stream160MiB e gráficos preservados no código.
Métricas não corrigem o gargalo por si: a fase do074 ainda estava em7,3–8,8FPS.
Próxima decisão deve considerar execução GPU e ocupação física do stream no Tegra.

Os contadores de descriptors cobrem os oito pontos do renderer nativo; não incluem SDK/presenter. writes conta estruturas VkWriteDescriptorSet, não a soma de descriptorCount.
Hash do NRO de métricas:273D76DFC49FE9AEA9A30F58754051B631FB382812DF39607CDDA1528D76CEC0.
Manifesto dos dois testes: outputs/frame-diagnostics/comparison.json (mesmo executável confirmado).
