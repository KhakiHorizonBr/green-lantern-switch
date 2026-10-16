# Próximo teste: FSI com pedido de vídeo 960×540

Estado: compilado e empacotado localmente; teste no Switch pendente.
Target: `green_lantern_switch_xenos_fsi`, opção CMake `GREEN_LANTERN_BUILD_FSI_VARIANT=ON`.
É separado de `green_lantern_switch_game`, que inclui o renderer nativo.

Mantém heap de 2000 MiB, FSI obrigatório, MSAA, CPU-only mapped reads e vigilância
de todos os aliases físicos. Não altera clocks ou o orçamento para fingir ganho.
Timestamps e timers de compilação/espera permanecem ativos; sampling invasivo desligado.

## Pedido e confirmação

O candidato define `video_mode_width=960` e `video_mode_height=540`. O objetivo é
o jogo criar alvos menores, não apenas reduzir o blit da janela. O jogo pode ignorar
esse pedido. As linhas `[gl-fsi-video] Actual guest swap` registram as dimensões
reais de apresentação; o perfil também informa largura/área dos desenhos.

Se o jogo adotar 960×540, o quadro principal terá 43,75% menos pixels que 1280×720.
Isso não significa igual redução no tempo de GPU: sombras e outros passes podem
continuar fixos. O HLE limita a altura mínima a 480; não pedimos 640×360, que seria
transformado silenciosamente em 640×480.

## Configuração opcional

Sem arquivo adicional, o candidato pede 540p. Para alternar no mesmo NRO, criar
`sdmc:/switch/green_lantern/fsi_video.ini` com uma linha:

```ini
video_mode = 720
```

Aceita `540` ou `720`. Não precisa editar o `tuning.ini`. Dados e saves continuam
nas mesmas pastas; cache FSI separado em `user/cache_xenos_fsi`.

## Interpretação antes de rodar

- Quadro/área menores e FPS maior: há custo relevante ligado a pixels/banda.
- Continua em 1280×720: o pedido não reduziu o trabalho interno; localizar ajuste do jogo.
- Adota 540p e segue lento: examinar passes fixos, sincronização e trabalho CPU.
- Imagem incorreta: voltar ao NRO FSI anterior; redução de qualidade não é correção.

O perfil adiciona `malloc_used_mib` e `malloc_free_mib`: livre agregado não é maior
bloco contíguo. Esses números não incluem necessariamente todas as alocações do driver.
O backing que já está no malloc não deve ser somado novamente.

NROs, ELFs e arquivos do jogo não são distribuídos neste repositório.
