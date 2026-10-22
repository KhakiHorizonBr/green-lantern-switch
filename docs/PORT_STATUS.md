# Estado atual — 17/10/2026

## Resumo

O jogo já inicia e chega ao menu e ao gameplay no Switch. O usuário relatou
gráficos corretos em uma execução anterior; depois informou que os defeitos
visuais voltaram em um candidato posterior. Ainda não isolamos qual mudança
causou a regressão. A prioridade é preservar a referência visual e só então
otimizar o desempenho.

## O que os dados confirmam

- O heap não estava sob pressão nos perfis analisados; aumentá-lo não explica
  nem corrige o FPS baixo por si só.
- No log 069, os render targets aparecem em 1x MSAA; o `nx_msaa` do renderer
  também inicia desligado.
- O perfil de GPU varia por cena. Em amostras pesadas, o passe principal
  1280×720 (`c=t1328p1280f d=t0p1280f`) levou 31,85–34,92 ms. O resolve de
  profundidade 1280×720 levou 15,64–21,89 ms. O log também registrou uma janela
  de 17,5 FPS e renderer CPU em torno de 52 ms por quadro.
- O passe principal inclui centenas de draws; o resolve de profundidade usa
  compute para copiar depth para R32F. Esse resolve também sustenta os dados
  empacotados de depth/stencil usados pela restauração do personagem.
- Os dumps FSI do frame 314 têm uma combinação de shaders repetida, com cinco
  fetches de textura e três `sqrt`. Ela é apenas candidata: esses dumps são de
  PC/FSI e ainda não foram correlacionados ao passe caro do Switch.

## Próximo passo

Correlacionar os draws/shaders do passe caro em uma captura do Switch e revisar
os custos de índices e gravação de comandos. O resolve de profundidade não deve
ser removido ou reaproveitado só pelo endereço: isso pode reintroduzir defeitos
de stencil e halos. Primeiro comparar o resultado gráfico com uma execução
determinística no PC; depois preparar um único candidato e manter o NRO de
rollback conhecido.

## Limites e rollback

- Não reduzir resolução, remover stencil ou alterar o resolve sem evidência de
  que a mudança preserva a imagem.
- Não tratar o shader candidato do frame FSI 314 como causa comprovada do custo.
- A última regressão visual relatada ainda precisa ser associada a uma opção ou
  mudança específica.
- Logs completos, capturas de tela, imagens de readback e executáveis ficam
  fora do repositório público; os resultados aqui são um resumo técnico.

Consulte [Resultados 054–059](RESULTADOS_054_059.md), [Diagnóstico 069](RESULTADOS_069_FPS.md),
[Comparação gráfica](GRAPHICS_COMPARISON.md) e [Changelog](CHANGELOG.md).
