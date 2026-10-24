# Buffers de vértices e apresentação — 23/10/2026

## Variante final: preenchimento de zeros na etapa de uploads

A primeira variante, copiando trechos inalterados de uma alocação GPU anterior,
passou no teste de bytes, mas piorou ligeiramente o tempo de GPU no PC
(aproximadamente8,0–8,3ms versus7,3–7,7ms). Foi descartada como candidato.

As capturas mostram buffers de aproximadamente1MiB com apenas34–50KiB de
bytes não zero. A variante final compara blocos de16KiB e mantém a sombra
exata. Copia os blocos com dados pela CPU e preenche os trechos inteiramente
zerados usando vkCmdFillBuffer. Não omite nenhum byte nem reduz a extensão
acessível aos shaders. Zeros com sinal/qualquer bit não zero são copiados.
Quando há poucos zeros ou fragmentação excessiva, faz o memcpy normal.

Os preenchimentos vão para o command buffer de uploads (setup), que é submetido
antes dos draws (main). A barreira existente no início de main torna as escritas
de transferência visíveis aos shaders. Os intervalos são novas alocações e não
dependem de uma cópia GPU antiga. Isso evita barreiras e drenagens por buffer.
O reaproveitamento de uma cópia integral idêntica ainda respeita stream_oldest.

Nenhuma troca de política de fila/apresentação foi feita nesta rodada: só foram
separados tempos dentro e fora da callback nativa para medir o hardware.

## Referência e objetivo

Hardware072 melhorou os resolves, mas ainda registrou14–68ms em vértices e
esperas de apresentação. Em duas janelas, copiou logicamente1,67–1,79GB de
vértices em10s, cerca de20–23MB por quadro. O modo seguro8 continua obrigatório:
os buffers indiretos permanecem completos para não reintroduzir o corpo preto.

## Novo caminho

- Sombra exata em RAM para buffers inteiros de256KiB a16MiB; orçamento total32MiB.
- Libera sombras sem uso há mais de120frames quando precisa abrir espaço.
- Compara blocos de16KiB contra a sombra antes de considerar reaproveitamento.
- Se todos os bytes permanecerem iguais e a cópia ainda estiver no stream,
  reaproveita seu endereço. Sem inferir limites de índices indiretos.
- Se uma pequena parte mudar, atualiza a sombra e cria uma nova alocação completa,
  com os blocos não zero copiados pela CPU e trechos zero preenchidos pela GPU.
- Buffers com mais de50% alterado deixam o caminho auxiliar e liberam sua sombra.
- Reutiliza um endereço antigo somente quando a cópia continua dentro da janela
  conservadora do stream, protegida por stream_oldest. Reconstruções em endereços
  novos não leem a cópia GPU anterior. Preenchimentos têm alinhamento de4bytes.
- Esgotamento do orçamento, falta de alocação auxiliar ou formato/tamanho fora
  do escopo conserva o caminho normal. A heap total e a arena não aumentam.

O buffer de stream ganhou uso TRANSFER_DST. Não há buffer GPU adicional.
O custo de banda do preenchimento pela GPU ainda precisa ser medido no Tegra.
Contadores `[gl-vertex-delta]` distinguem bytes escritos pela CPU, preenchidos pela
GPU (zeros) e reaproveitados. O contador legado de vértices continua sendo lógico.

Também corrigida referência a VertexHash que era mantida através de uma
possível inserção/reorganização do FlatMap em DrawVertexRange: agora é obtida
somente dentro de whole_hash, quando necessária. Isso também evita inserir
entradas nunca usadas por alguns recortes diretos pequenos.

Apresentação: separa duração da callback nativa e tempo fora dela. Nenhuma
espera obrigatória de GPU foi removida nem profundidade de fila alterada.

## Controles e validação

PC: desativado por padrão; os casos de comparação ativam explicitamente
`nx_vertex_delta_upload=true`. Switch: ativado por padrão neste candidato.
`vertex_delta_upload=0` no tuning.ini desliga a mudança e preserva as outras
otimizações. Rollback072 em `outputs/fps-vertex-delta`.

Sonda só PC salva o resultado GPU e a sombra esperada da mesma reconstrução
no frame2000. Script `compare-vertex-buffers.py`. Variante de zeros no main:
quatro buffers, 4.160.896bytes, zero diferenças, execução100s com saída0.
Variante final validada no caso `vertex-zero-final-verify`: quatro buffers,
4.160.896bytes, zero diferenças. Replay de110s chegou à fase e encerrou com
saída0; imagem final inspecionada. Comparação não cobre todas as cenas do jogo.

Na última amostra acumulada, este caminho registrou263.619.876bytes escritos
pela CPU, 5.765.661.488bytes preenchidos pela GPU e216.366.592reaproveitados,
com31MiB de sombras. Esses contadores cobrem somente o caminho auxiliar,
não incluem todos os uploads do renderer e não são uma medida de FPS.

Não foi demonstrado ganho de FPS no PC: os testes intermediários ficaram
ligeiramente mais lentos; GPU final7,5ms ainda na faixa da referência072.
O candidato busca reduzir escrita da CPU no stream do Switch, mas compara
e lê memória adicional e transfere trabalho para a GPU. Pode não melhorar
ou pode regredir no Tegra. Precisa de medição no console para decidir mantê-lo.
A sombra custa até32MiB adicionais sob demanda, dentro da mesma heap de2000MiB.
## Hardware073 — 23/10/2026

Usuário percebeu nova melhora. Menu/trechos leves chegaram a24,6–24,8FPS;
fase pesada teve três janelas do profiler em8,0FPS. Renderer teve79/81/78
swaps por aproximadamente10s. Referência072 nas três janelas finais:81/77/83.
As cenas não são replay idêntico; não há evidência de ganho sustentado na fase.

CPU renderer122,4/118,1/123,5ms; GPU77,8/75,9/79,5ms. Vértices46–48ms;
apresentação fora da callback16,6–30,2ms, callback nativa0,14–0,15ms.
Tempos de CPU/GPU se sobrepõem e esperas podem migrar entre seções.
Clocks1020MHz CPU /460,8MHz GPU /1331,2MHz memória, modo portátil.
Leituras emuladas falhas0 e auto-memory pressure=false.

Novo caminho enabled=true. Última amostra disponível:31MiB sombras,
11.747.328bytes CPU,482.927.232bytes zeros GPU,reused0. Os contadores não
crescem entre as últimas duas amostras disponíveis, apesar da continuidade
dos uploads lógicos na fase. Investigar cobertura/fallback e ocupação das
sombras antes de atribuir todo o custo a memcpy ou aumentar orçamento.
O log atual não distingue motivo de fallback; hipótese ainda não confirmada.

Profiler indica muestreo de pilas desactivado: percentuais de threads presentes,
mas sem amostras de funções. Desconsiderar primeira linha0s com taxas absurdas,
um artefato do intervalo inicial sem tempo decorrido.

Próximo alvo: identificar buffers/motivos que escapam da otimização na fase e
as esperas dentro do presenter. GPU sozinha já excede33,3ms na fase pesada;
resolver CPU não basta para garantir30FPS. Não remover sincronização sem
verificar dependências. Logs originais em outputs/fps-vertex-delta/hardware-073.
