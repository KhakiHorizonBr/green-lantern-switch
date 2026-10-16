# Estado atual — 16/10/2026, execução 059

## Resumo

Dois renderers estão preservados: o nativo adaptado dos Simpsons é mais rápido,
mas tem defeitos gráficos; Xenos/Vulkan FSI produziu gameplay visualmente correto
na sessão 058, com FPS muito baixo. Não é um port concluído.

## Fatos confirmados

- PC: D3D12 correto, Vulkan host preto, Vulkan FSI correto.
- Switch: FSI selecionado e suportado; usuário chegou ao gameplay sem defeitos
  visuais observados e sem crash na sessão 058. Isso não prova estabilidade global.
- CPU-only mapped reads removeu as leituras emuladas medidas, preservando watches físicas.
- Log059: 137 pipelines distintos, todos criados com sucesso; média1,28s no driver.
- Último intervalo aquecido:1,4FPS com0compilações e8,97s em espera por submissões.
- Heap2000MiB mantida. Nenhuma indicação de que aumentar capacidade resolveria esse trecho.
- Não há correção confirmada da origem do crash057 em sub_8233E7D8.

## Próximo passo concreto

Candidato compilado pede960×540 e registra o frontbuffer real, áreas/larguras dos
passes e malloc usado/livre. Execução pendente. Não anunciar resolução menor ou
melhora de FPS antes de conferir os resultados. Ver [FSI_540P.md](FSI_540P.md).

## Regras para não repetir caminhos sem evidência

- Não ligar readonly_mapped global no Xenos: perde vigilância das escritas físicas.
- Não contar GPU Commands como utilização física da GPU; é uma thread de CPU.
- Não somar timings inclusivos nem interpretar spanGPU como tempo puro ocupado.
- Não atribuir o FPS aquecido a compilação quando misses/driver_ms são zero.
- Não tratar crashes acumulados antigos como falha da sessão atual.
- Não aumentar heap/arena sem pressão ou falha de alocação demonstrada.
- SHM/permissões, mapeamentos e arena de backing já têm histórico de limitações;
  consultar as notas anteriores antes de repetir sondas de plataforma.

## Evidências e histórico

- [Resultados054–059](RESULTADOS_054_059.md)
- [Referências externas e aplicabilidade](REFERENCIAS_EXTERNAS.md)
- [Comparação gráfica](GRAPHICS_COMPARISON.md)
- [Estado histórico até052](PORT_LOG_052.md)
- [Logs e métricas059](../logs/run-059/)
