# Medição050 e comparação com os ports de referência

O usuário entrou na gameplay sem queda. Nenhuma falha de alocação registrada; crashanexado é do049(base165823D000), perfil050 usa125987000. Heapconfigurada2000MiB; mallocatingiu1374,5MiB. Estes números não são o consumo mínimo do jogo inteiro.

| Categoria | Último MiB | Pico MiB | Falhas |
|---|---:|---:|---:|
| vk_other | 0.0 | 0.0 | 0 |
| buffers_other | 16.0 | 16.0 | 0 |
| upload_staging | 0.25 | 57.24 | 0 |
| render_targets | 96.69 | 96.69 | 0 |
| sampled_images | 129.19 | 129.19 | 0 |
| storage_images | 144.62 | 171.81 | 0 |
| native_upload_ring | 112.0 | 112.0 | 0 |

Não somar picos de momentos diferentes. Tracker não teve overflow; cobre VkMemory da aplicação interceptado, não cada malloc do driver/compilador. Ring112MiB corresponde96+16slack. Backingguest432MiB de pools dentro da arena448MiB. Aliases não são RAMduplicada. O malloc inclui backing da arena e memória NVK; não somar tudo de novo ao total.

## Comparação local

- Project8: src/game/src/native/native_textures.cpp usa descarte depois de600quadros e fila Retire/DestroyLater após GPUconcluída; ring160+16MiB. Fontes locais work/project8-sw-reference. A camada nativa do Lanterna já deriva dessa base e da versão dos Simpsons; não faltava copiar uma política LRU porbytes já existente ali.
- Simpsons: fonte local the-simpsons-game-nx-main/src/game/src/native/native_textures.cpp também usa600quadros e2criações pequenas síncronas porquadro. Isto é prazo fixo, não orçamento global automático.
- COD4: releaseconfig consultada anteriormente usa cachevertices128MiB, pooltexturas inicial128MiB e compilaçãoasyncfalse. Estes knobs são específicos do port. Não demonstram que128MiB serve a todos os buffers do Lanterna.

## FPS e imagem

Sem readfaults emulados. Há intervalos de gameplay de1,4 a14,5apresentações/s; não afirmar15–20constantes. CPUrenderer chegou374ms/quadro em intervalo de compilação; GPU38,8..80,5ms nos intervalos finais, vértices3,8..81ms e quase1GiB copiado por10s em algumas cenas. Texturas/passes/formatos continuam incorretos. Aumentar RAM não corrige trabalho gráfico nem garanteFPS.

## Primeira política adaptativa implementada

Heap2000 preservada. Modoauto escolhe ring160MiB quandoheap>=1792,96nosmenores, mantendo16slack e todos os fences. Escolha inicial, não resize de buffer em uso. OverrideINInative_stream_mib mantém controle. CSV050 sugere margempara64MiBadicionais; impacto emFPS ainda não medido.

Cache: pressão entra em85%doheap e saiabaixo75%; usa mallinfo.uordblks, não totalfree. Empressão reduz idade600->60quadros e varredura60->15; só retira imagens antigas, já fora dequadros GPUpendentes. Invalida respostas/ponteiroscacheados antesdereuso CPU. Reaproveita Retire/Reaper dosports, semliberarrecursos ativos. Essa é uma heurística com histerese, não limiteótimo comprovado. No050 o pico68,7% não acionaria descarte porpressão.

Streaming: reserva porjob cobre queued+stagingatéliberaçãoapósfence, cap64MiBnormal/32pressão. Umrecurso maior pode entrar sozinho; jobssemcota adiam e tentamnovamente, semmarcartexturacomoerro. Jobsconcluídosnão solicitadoshá30quadros são descartados, evitando retercotaparasempre. Registrar atividade em[auto-memory]. Pode haver atraso de texturas se a cota apertar; observar antesdebaixarmais.

Não há elasticidade da arena nem cache porbytesdeTODOSos recursos. Shaders, resolves e buffersdeprofundidade requerem regraspróprias. Sem alterações de semânticaMSAA/resolve/iluminação neste candidato. Logs repetidos de exact-sizecopy foramlimitados, semmudar a cópia.
