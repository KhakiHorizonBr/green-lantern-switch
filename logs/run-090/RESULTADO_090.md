# Log 090 — identificação do executável, 05/11/2026

O log registra o backend deko3d v9: criação de dispositivo/queue deko3d e
`[deko-game-v9] ready: 122 shader variants; ... batch_ops=1 ...`.
Não contém os marcadores de boot `[gl-internal-scale-v1]` e `[gl-ablation-v1]`,
nem as novas janelas `[gl-fps-second-v1]` da variante Vulkan entregue.

Conferência do artefato local `outputs/internal-scale/green_lantern_escala_interna.nro`:
54.671.557 bytes, SHA-256
`07c73357cdc956d85312d5a2ab00a7ba5df1c427e6b16d8e7b881729dbde2fc0`.
Contém os três marcadores Vulkan acima e não contém `[deko-game-v9]`.

Portanto, este arquivo não permite avaliar o efeito da escala Vulkan. Pode
haver mistura entre o log anexado, um atalho ou o NRO aberto; o log sozinho não
identifica qual ocorreu. Não atribuir a falta de mudança à técnica de escala,
nem lançar estes dados como resultado de performance da variante nova.

Repetir com o NRO de nome `green_lantern_escala_interna.nro`, título NACP
`Green Lantern - Escala Interna`, mantendo os arquivos do fork deko3d separados.
Usar `internal_scale_percent=75`, `ablation_mode=0`, `partial_submit_draws=0` no
tuning.ini existente. O log correto deve registrar no boot
`[gl-internal-scale-v1] percent=75 applied=true` e, depois, contadores de scopes
e blits. Enviar o novo log da execução.

O parser anterior emitiu janelas vazias para este arquivo por falta dos novos
marcadores; FPS null não significa zero FPS ou falha de renderização.
