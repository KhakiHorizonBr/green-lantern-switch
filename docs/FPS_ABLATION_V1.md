# Próximo diagnóstico Vulkan — 30/10/2026

Entrega isolada: `outputs/perf-ablation/green_lantern_diagnostico.nro`, a partir
do snapshot Vulkan dos testes 086/087. Instruções completas e fontes privados
em `outputs/perf-ablation/LEIA-ME.md` e `native_experiment/native/`.

O usuário relata gameplay normalmente perto de vinte e poucos FPS, com quedas
mais fortes em momentos específicos. Os registros 086/087 contêm janelas
pesadas próximas de oito FPS, mas isso não estabelece o desempenho típico da
sessão inteira. A próxima medição deve distinguir duração das quedas, loading,
menu e gameplay normal.

O NRO acrescenta janelas de aproximadamente um segundo usando tempo medido,
histograma de intervalos/p50/p95/p99 e leitura de clocks CPU/GPU/EMC com códigos
de erro. Não altera os clocks. Mantém os timestamps GPU e contadores de CPU
existentes. Não implementa profiler por amostragem ou shader trivial.

`ablation_mode=0` mantém o desenho normal; `1` omite comandos guest de draw após
preparação/binds; `2` reduz somente a largura do scissor à metade; `3` omite a
rotina de resolve inteira, com os clears nela associados. São ablações, com
imagens inválidas esperadas. Não equivalem a otimizações prontas para jogar.

Primeiro teste solicitado: modo 0 e `partial_submit_draws=0`, cerca de dois
minutos de gameplay com trecho normal e pesado. Verificar os clocks disponíveis
e a proporção de janelas rápidas/lentas antes de pedir as ablações no hardware.
O NRO anterior permanece preservado em `outputs/partial-submit/`.

Fontes compartilhados com deko3d não foram alterados. O arquivo
`outputs/perf-ablation/VALIDACAO.md` registra as verificações locais; apenas
execução no Switch poderá validar os serviços de clocks e os tempos no Tegra.
