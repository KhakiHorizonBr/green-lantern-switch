# Reaproveitar classificação dos vértices — 24/10/2026

## Correção da interpretação do073

Os contadores antigos saíam a cada300frames. O trecho de gameplay no073
teve cerca de240swaps nas três últimas janelas e acabou antes do próximo
relatório. Portanto, a ausência de crescimento nos últimos contadores NÃO
prova que o caminho deixou de funcionar na fase. As hipóteses de orçamento
e buffers dinâmicos ainda precisam de contadores cobrindo essa parte.

## Mudança concreta

A classificação de blocos de16KiB em zero/dados passa a permanecer na sombra.
Na primeira cópia classifica todos; nas seguintes reclassifica apenas blocos
que a comparação exata detectou como alterados. Isso evita reler grandes
áreas imutáveis procurando zeros a cada reconstrução do buffer GPU.

Quando o modo exige hash integral (bit4 de nx_vertex_fetch_safety), a mesma
identidade de hash também dispensa a comparação redundante com a sombra.
No modo8 atual, hashes podem vir do detector por amostragem: NÃO bastam para
pular a comparação exata. Ela continua obrigatória antes de reaproveitar a
sombra, principalmente quando a cópia GPU antiga já saiu do stream.

Preserva o contrato de cache por hash do renderer; não muda os limites de
fetch, shaders, GPU fills, barreiras, heap2000MiB ou arena384MiB. Classificação
auxiliar de1KiB só existe para sombras alocadas: até128KiB além das sombras
limitadas a32MiB. Nenhuma alteração no SDK/presenter nesta rodada.

## Medição

Novos [gl-vertex-window] saem nas janelas de10s do renderer e mostram:
- CPU em hash, caminho auxiliar e alocação/cópia do fallback;
- bytes realmente escritos pela CPU, zerados pela GPU e reaproveitados;
- motivos de fallback: opção desligada, tamanho, buffer dinâmico,
  orçamento da sombra, falha de alocação e maioria dos blocos alterada.

Os tempos não devem ser somados a outras seções já abrangentes do perfil.
A inspeção do presenter confirmou que RefreshGuestOutput pode executar
PaintAndPresent no thread produtor. Lá há espera de submissão antiga e
vkAcquireNextImageKHR; isso explica possibilidades para o tempo fora da
callback, mas ainda não mede qual espera domina no Switch. Sincronização
foi preservada. GPU pesada no073 já custa76–80ms e também precisa otimização
para alcançar33,3ms por frame; melhorar só a CPU não garante30FPS.

## Evidências e artefatos

Validação final concluída abaixo. Rollback073 preservado em outputs/fps-vertex-cache.

## Cobertura do orçamento e caminho sem sombra

A medição PC intermediária (vertex-cache-final-verify) chegou à fase e mostrou
1,78–1,90GB por10s voltando à cópia normal por orçamento. Assim a investigação
já identificou um motivo real no PC; sua proporção no073 ainda não era medida.

A variante seguinte admite sombras apenas para buffers com pelo menos50% de
blocos inteiramente zero e até16intervalos de zeros. Buffers densos seguem a
cópia normal sem ocupar a sombra; o motivo aparece como dense nos contadores.
O teste intermediário confirmou que somente essa seleção não elimina todos
os casos de orçamento esgotado.

Variante final: após inspecionar um buffer esparso, se não houver espaço para
sombra, aloca sua extensão completa no stream GPU, copia os intervalos com
dados da memória original e preenche os intervalos zero na GPU. Nenhuma sombra
persistente é necessária. Isso reaproveita a inspeção que já foi feita para
admissão e remove o fallback por orçamento desses buffers esparsos. Contador
uncached_sparse_bytes mostra a cobertura. Mantidos alinhamento, setup antes
de main, barreira de transferência e proteção contra reutilização do stream.

A validação PC salva expected.bin da memória ORIGINAL do guest no momento
do upload, além do readback GPU, e identifica uncached=true no JSON desses
buffers. Assim a prova não compara apenas uma sombra contra sua própria cópia.
Esse recurso de validação só existe no PC e está desligado no NRO.

## Resultado final

Caso vertex-cache-uncached-final,150s, saída0, chegou à fase. Imagem inspecionada.
Comparação contra memória original:6buffers,7.889.024bytes, zero diferenças;
4com sombra e2sem sombra (1.966.080 e1.762.048bytes, uncached=true nosJSON).
Relatório: outputs/fps-vertex-delta/vertex-cache-uncached-final-byte-comparison.json.

Duas últimas janelas: uncached_sparse_bytes1.658.358.912 e1.684.637.824;
budget fallback0. CPU escrita do caminho esparso388.415.488 e366.493.696bytes;
zeros GPU2.580.247.168 e2.632.907.648bytes. Contadores não abrangem cópias
parciais nem todas as transferências do renderer. Não converter em ganhoFPS.
CPU renderer7,9/6,7ms; GPU8,1ms. Sem ganho sustentado de desempenho PC
comprovado contra o candidato073. A comparação tem cenas/carga variáveis.
O alvo é reduzir escrita da CPU no stream do Switch; custo de varrer fonte e
preencher GPU pode anular economia, portanto hardware ainda precisa medir.

A primeira tentativa de replay ficou no menu. A execução final usou
outputs/native-pc-user/gameplay-vertex-cache.replay com confirmações repetidas.
Uma execução intermediária foi interrompida por mudança de permissões da sessão;
a validação final foi retomada e concluída. Não usar logs incompletos como prova.

NRO final: outputs/fps-vertex-cache/green_lantern_fps_cache_seletivo.nro.
SHA256:92ED01C61C4997356E49659BB4E2017E729EF87CFECFF77DD9D6BEDFD8795F0B.
## Hardware074 e controles — 24/10/2026

Usuário percebeu melhora, mas perfil ainda mede menu24,6–24,8FPS e fase pesada
8,8/7,7/8,2/7,3FPS. Não há ganho sustentado comprovado frente ao073 (~8FPS).
CPU renderer109,9/124,6/117,5/130,5ms; GPU70,5/79,6/75,9/77,9ms.

gl-vertex-window confirma upload esparso ativo: fallback por budget0,
uncached_sparse_bytes0,95–1,13GB/10s. Escrita CPU73–165MB e GPUzeros1,09–1,31GB
por janela. Economia de bytes não virou ganho global sustentado: caminho
auxiliar16,6–53,8ms/frame inclui Allocate e possíveis esperas pelo stream.
Há quadros com espera de59–69ms explicitamente atribuída ao espaço do stream.
Não tratar toda a seção de vértices como memcpy ou CPU ativa. Hash7,2–9,8ms,
registro12,7–14,1ms; GPU também excede33,3ms. Próxima investigação precisa
separar tempo ativo da varredura/registro e espera de alocação, além de GPU.

Logs preservados em outputs/fps-vertex-cache/hardware-074.

Usuário escolheu posição Xbox: Switch B->guest A, A->B, Y->X, X->Y.
Novo candidato de controles aplica input_xbox_layout=true explicitamente
no OnPreSetup, após configuração do SDK, antes de criar o sistema de input.
O tuning.ini permite input_xbox_layout=1 (posição) ou0 (mesma letra).
Boot registra gl-input com a correspondência e resultado de aplicação.
Não atribuir causa do relato anterior a configuração antiga sem ler o SD;
a versão074 não registrava o valor dessa opção. Nenhuma mudança no renderer,
heap, arena ou GPU neste candidato. Compilação não confirma botões no hardware.
