## Atualização de 16/10/2026 — 054 a 059 e candidato 540p

- Atlas de resolves, views R16 assinadas e capturas de estados/readbacks para comparar PC.
- Isolamento dos diretórios de saída dos builds PC no SDK para evitar colisões entre backends.
- Variante separada Xenos/Vulkan FSI com seleção obrigatória e cache isolado.
- CPU-only mapped reads preserva a vigilância física e elimina as leituras emuladas medidas.
- Timers inclusivos, hits/misses e resultados de timestamps GPU no perfil.
- Logs059 demonstram custo de compilação e lentidão sustentada mesmo com cache aquecido.
- Pedido960×540, registro do swap real e métricas malloc: compilados, hardware pendente.
- Documentação atualizada, com limites dos resultados e referências Rayman/XenosRecomp.
# Mudanças desde a publicação inicial (035)

## Plataforma e estabilidade

- Cabeçalho de plataforma encaminhado ao SDK, corrigindo divergência de ABI/tamanho de threads.
- Reserva antecipada das pilhas dos workers gráficos e registro dos erros libnx.
- Preservação de Q0–Q31, FPCR/FPSR nas exceções; retomada por stubs por thread.
- Ajustes nas opções do código recompilado: strict aliasing desativado e contração FP desativada.
- Diagnósticos e correções locais em funções geradas permanecem fora da distribuição pública.

## Memória e desempenho

- Arena antecipada de backing com alinhamento, free-list, coalescing e logs de esgotamento.
- readonly_mapped para o renderer nativo: retirou o grande volume de faults de leitura.
- Cache de formato do presenter corrigido: passou de 2032 recriações observadas no 048 para
  uma criação no 049. Compilação gráfica serializada; pipelines assíncronos desativados.
- Heap configurável no CMake; teste de 2000 MiB passou dos crashes observados com 1280 MiB.
- Telemetria geral e por área Vulkan (7 categorias), com picos, falhas e limite do rastreador.
- Seleção inicial automática do ring (160 MiB com heap maior; 96 MiB com heap menor).
- Histerese de pressão, limpeza de texturas antigas respeitando fences e quota de staging.
  Isso não redistribui automaticamente toda a RAM do processo.

## Gráficos e diagnóstico

- Resolve passa a considerar formato de destino e expoente; restaura escala de fixed16/SNORM.
- Shader de cópia e header SPIR-V genéricos atualizados; helper de recompilação incluído.
- Pouca melhora visual no 052: alias EDRAM, dimensões e composição ainda em investigação.
- Nova captura acionada por botões, readbacks limitados, imagens brutas e layout explícito.
- Decoder Python gera previews e estatísticas amostradas, incluindo NaN/Inf e valores fora de 0–1.
  Não altera o jogo, e o preview não aplica automaticamente gamma/swizzle/exposição guest.

## Evidências de hardware

| Execução | Resultado relevante |
| --- | --- |
| 037–040 | Primeira imagem; sequência de correções de estabilidade |
| 041–043 | Menu/início, desempenho muito baixo e faults excessivos |
| 045–046 | Menus mais rápidos; arena e compilador ainda limitavam progresso |
| 048–049 | Falha no compilador Mesa sob orçamento de 1280 MiB; cache do presenter corrigido |
| 050 | Heap 2000 MiB: usuário chegou ao jogo sem o crash anterior |
| 051 | Política adaptativa inicial; erros gráficos persistem |
| 052 | Conversão de resolve ativa, pouca melhora visual; foco passa à comparação com PC |

O NRO de captura foi compilado depois do 052; seu comportamento no console ainda não foi confirmado.

