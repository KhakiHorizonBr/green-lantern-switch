# Desempenho: índices e resolve de profundidade — 22/10/2026

## Referência

O usuário confirmou os gráficos corrigidos no Switch no teste068. O perfil069
aponta carga tanto na CPU quanto na GPU. No trace PC do frame2700 há três
resolves de profundidade1280x720 para1F6F8000. Entre o segundo e o terceiro,
draw669 só escreve cor e draw670 tem depth sem escrita Z nem stencil.

## Correção da hipótese anterior

`0x00008701` tem stencil habilitado (bit0). A afirmação anterior de stencil
desabilitado estava incorreta. A cache conservadora registrou zero reutilizações
no replay, então **fica desligada por padrão**. Não remover o terceiro resolve
somente pela igualdade do endereço ou ausência de escrita Z.

## Mudanças candidatas

1. `nx_depth_resolve_cache`: uma única entrada, limitada ao mesmo frame,
   mesma imagem de origem/destino, mesma imagem de palavras empacotadas,
   mesmos controles, pitch e retângulo. Somente float24 com saída empacotada
   produzida com sucesso e sem clear associado.
2. `nx_index_fast_copy`: quando o marcador de reinício guest já é0xFFFF
   (16bits) ou0xFFFFFFFF (32bits), a conversão não precisa substituí-lo.
   Usa os mesmos caminhos memcpy/byteswap empregados sem primitive restart.
   Marcadores diferentes e conversões fan/loop continuam pelo caminho antigo.
3. `nx_depth_packed_direct`: shader compute amostra separadamente profundidade
   D32_FLOAT e stencil S8 e escreve diretamente R32_FLOAT e RGBA8 com as palavras
   float20e4/stencil. Conserva o mesmo encode20e4, clamp e ordenação de bytes.
   Evita as cópias de imagem para buffer temporário e de buffer para as duas
   imagens. Se não houver pipeline/views compatíveis, usa o caminho antigo.
   Somente float24 de uma amostra com restauração de stencil habilitada.

## Invalidação auditada

- Draw com escrita Z habilitada: incrementa a versão global de profundidade.
- Draw com stencil habilitado: também incrementa, inclusive operações KEEP
  (conservador: pode perder oportunidades, nunca depende de prever o teste).
- Restauração explícita de stencil por cópia: incrementa a versão.
- Clear explícito de profundidade/stencil no resolve: incrementa a versão.
- Recriação/crescimento da imagem de origem: handle diferente impede o hit.
- Outro resolve no mesmo endereço de destino: invalida a entrada antes de gravar.
- Troca do destino empacotado: handle diferente impede o hit.
- Novo frame: a entrada anterior não pode ser usada.

O modelo nativo não grava diretamente em imagens de profundidade por meio das
cópias de cor do cache EDRAM. As limpezas por depth que também zeram aliases
de cor já passam pela invalidação de draw acima. A reutilização preserva a
propagação para views menores e o caminho comum de barreiras e capturas.

Não é uma cache de memória guest nem uma deduplicação apenas por endereço.
Não muda resolução, heap, arena, restauração de stencil ou segurança dos vértices.

## Controles de rollback

No PC: `--nx_depth_resolve_cache=false --nx_index_fast_copy=false`.
No Switch, acrescentar ao `tuning.ini` existente (sem substituir suas outras opções):

```ini
depth_resolve_cache=0
index_fast_copy=0
depth_packed_direct=0
```

Cache usa0 por padrão; índices e conversão direta usam1. Contadores `[gl-depth-cache]` e `[gl-index-copy]`
registram reutilizações e bytes dos caminhos em bloco/escalar a cada300frames.

## Validação

Replay PC de100s com conversão direta chegou à fase e encerrou por solicitação
da automação, saída0. No trecho final estável, GPU total passou de11,2–11,3ms
para9,0–9,3ms; o bloco de três resolves de profundidade passou de2,23ms para
0,16ms nas amostras. São execuções distintas da mesma cena/replay, não quadros
idênticos. Índices na CPU ficaram por volta de0,5ms nos dois caminhos no PC.
Não prometer aceleração de índices nem extrapolar o ganho de GPU para o Switch.

Comparação byte a byte concluída: sonda Windows grava as duas saídas diretas
e em seguida executa o caminho antigo sobre a mesma origem, no mesmo resolve.
Nenhuma sonda dupla é habilitada no Switch. Script `compare-depth-outputs.py`
em `outputs/fps-depth-cache`. Builds e casos locais nessa pasta e em
`outputs/native-pc-user/depth-direct-*`.

No frame2000, resolves3/14/16, todos os seis buffers comparados são idênticos:
3.686.400bytes por buffer, 22.118.400bytes no total, zero diferenças. Há conteúdo
não zero em todos eles. Relatório: `outputs/fps-depth-cache/depth-byte-comparison.json`.
As saídas de profundidade dos resolves14/16 têm o mesmo hash, mas suas palavras
empacotadas têm hashes diferentes: o stencil realmente mudou. Isso confirma que
deduplicar a operação inteira sem rastrear stencil teria sido incorreto.

Build Switch concluído; candidato `outputs/fps-depth-cache/green_lantern_fps_depth_direto.nro`.
Validação no NVK/Tegra ainda pendente. A comparação dupla só existe na sonda PC.
