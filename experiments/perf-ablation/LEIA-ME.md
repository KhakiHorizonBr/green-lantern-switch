# Diagnóstico Vulkan: tempo de quadro e ablações — 30/10/2026

## Para o primeiro teste

Use `green_lantern_diagnostico.nro`, aberto como aplicativo pelo R no jogo.
Na pasta do SD `sdmc:/switch/green_lantern/`, **edite o tuning.ini existente**:

```ini
ablation_mode=0
partial_submit_draws=0
```

Se essas chaves já existem, altere-as; evite duplicatas. Preserve as outras
configurações. Jogue normalmente por cerca de dois minutos, incluindo uma
parte que você considera fluida e uma parte pesada. Envie o novo
`green_lantern_NNN.log` e diga aproximadamente quando entrou na fase.
Não é necessário ligar os outros modos neste primeiro teste.

O modo zero mantém desenho, memória, shaders, resolução e política de clocks
do baseline anterior. O novo código adiciona observação; não promete ganho de
desempenho. O NRO anterior está preservado na pasta `../partial-submit/`.

## O que mudou no contador

O relatório antigo conta swaps em uma janela descrita como “last 10 s”, mas
a thread só verifica o prazo entre blocos de trabalho. A janela pode passar
de dez segundos. A análise dos 086/087 já usou os timestamps reais para os
intervalos disponíveis; não dividiu tudo cegamente por dez.

O novo `[gl-fps-window-v1]` registra duração real, número de swaps e FPS da
janela. A primeira janela longa não tem duração inicial registrada e é
omitida desse contador adicional.

O `[gl-fps-second-v1]` soma intervalos consecutivos até completar pelo menos
um segundo. Mostra duração medida, quadros, média e quantos intervalos ficaram
em até 35 ms, 35–50 ms, 50–100 ms e acima de 100 ms. Também registra p50/p95/p99.
A última janela incompleta não é emitida. Um travamento sem novo quadro só
aparece quando o próximo quadro chega; isto não é um watchdog.

São intervalos observados na thread após apresentação, **não medição do painel,
do scanout nem da simulação do jogo**. Uma janela de oito FPS não descreve toda
a sessão. A percepção do usuário e o trecho específico importam; separar menu,
loading, gameplay normal e quedas antes de concluir um desempenho típico.

`analyze.py CAMINHO_DO_LOG --output resultado.json` valida contagens e a fórmula
frames/segundos, e resume quanto tempo de janelas ficou em cada faixa. Essas
faixas classificam médias de janelas, não o tempo individual de cada quadro.

## Clocks

`[gl-clocks-v1]` lê CPU/GPU/EMC com clkrst no primeiro quadro e em cada relatório
longo, junto do modo de operação/desempenho. Cada leitura registra o Result;
zero Hz com falha não é frequência real. São taxas informadas pelo serviço,
não utilização ou frequência física amostrada continuamente. Não altera
clocks nem perfis. A disponibilidade e os valores precisam ser verificados
no Switch; a compilação no PC não valida permissões de serviços Horizon.

## Modos opcionais: um de cada vez

Altere `ablation_mode`, feche e reabra o mesmo NRO. A configuração é lida no
início, não muda durante a sessão. Compare a mesma cena e os mesmos clocks.

| Valor | Intervenção | O que ajuda a investigar |
| --- | --- | --- |
| 0 | Baseline, nenhuma ablação | FPS normal e duração real das quedas |
| 1 | Omite vkCmdDraw/Indexed após preparação e binds | Custo residual de preparação, estado, resolves e apresentação; não mede CPU pura |
| 2 | Metade da largura do scissor, altura preservada | Sensibilidade a fragmentos/pixels; não é resolução interna reduzida |
| 3 | Omite a rotina de resolve inteira, incluindo clears associados | Custo agregado dessa rotina; não separa depth, cor, cópia e clear |

Os modos 1 e 3 podem produzir tela vazia, impedir navegação ou mudar o caminho
do jogo. O modo 2 corta partes da imagem. Não interprete FPS maior nesses
modos como otimização jogável. Os contadores de draws continuam contando
trabalho preparado/tentado; no modo 1 não representam draws emitidos à GPU.

Se modo 2 reduzir bastante o tempo GPU com cena equivalente, há justificativa
para implementar escala real, com coordenadas de resolve/depth coerentes. Se
não reduzir, isso não elimina custos de banda/cópias que o scissor mantém.
Se modo 1 ainda for lento, investigar preparação, submits/esperas e trabalho
restante antes de atribuir tudo a shaders. Se modo 3 ajudar, separar os
componentes do resolve em uma etapa posterior. Nenhum resultado isolado
decide entre Vulkan e deko3d.

Shader trivial e profiler por amostragem **não fazem parte deste NRO**. Os
tempos por seção e timestamps GPU existentes continuam disponíveis. Um
sampler de PC das threads exige validação própria para distinguir execução,
espera e permissões; não foi improvisado neste experimento.

## Isolamento e validação

Fontes privados partem do snapshot Vulkan que gerou o NRO dos testes 086/087.
Os quatro arquivos nativos e o main do Switch são recompilados fora da árvore
compartilhada. `prepare.py`, `build-switch.py` e `build-private.py` preservam
receitas e manifestos de insumos. Objetos guest e bibliotecas compiladas são
reutilizados; isto não é uma reconstrução independente de todo o SDK.

O fork deko3d e seus arquivos não foram modificados. As compilações PC e Switch
são sequenciais para evitar esgotar os 16 GB do computador.
