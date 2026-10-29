# Geometria GPU residente — avaliação de 29/10/2026

## Resultado e decisão

Implementado um protótipo Vulkan independente do fork deko3d, em
`outputs/geometry-resident/native`. O caminho novo passou na verificação dos
dados lidos da GPU e não apresentou regressão visual aparente nas imagens
inspecionadas. **Não demonstrou ganho claro de FPS no PC.** Continua desligado
por padrão, sem substituir o renderer compartilhado e sem NRO novo promovido.

As submissões parciais da rodada anterior permanecem desligadas. Não misturar
este experimento com a investigação do kickoff deko3d do hardware076.

## Implementação

`nx_geometry_resident_mib=32` divide o buffer de upload existente em128MiB de
ring transitório e32MiB residentes. A folga original de16MiB é preservada no
fim: total176MiB, igual ao padrão de160+16MiB. A variante não aumenta a heap,
não cria sombras CPU do tamanho da geometria e não altera shaders/resolução.
`0` mantém os160MiB transitórios, com a opção desligada.

O renderer continua copiando intervalos inteiros quando o fetch indireto exige
isso; o experimento não reduz os limites que corrigiram os personagens pretos.
A arena residente usa páginas de64KiB, primeira sequência livre contígua e
reunião natural de páginas adjacentes. Um bloco alterado é aposentado, nunca
sobrescrito enquanto sua última submissão puder usá-lo. Trabalho gravado que
ainda não foi enviado também é protegido. Não há espera de fence feita só
para liberar espaço do cache: sem bloco disponível, usa o upload habitual.

Admissão após conteúdo igual em duas observações de quadros diferentes;
verificação por XXH3 da região inteira, sem detector amostrado. O hash não é
prova matemática de igualdade. A convenção de verificar memória uma vez por
quadro é a do renderer-base; não foi introduzido rastreamento completo de
stores guest. Alterações levam a cooldown de16quadros. Limites desta primeira
política:1024identidades,256KiB a metade do orçamento por buffer; evicção de
blocos exige idade e conclusão GPU.

O cache foi aplicado ao caminho de buffers integrais de vértices. Não cobre
índices nem as cópias parciais menores já escolhidas pelo renderer-base.

## Isolamento e build

Foi usado o snapshot Vulkan da rodada anterior, pois os fontes compartilhados
já contêm alterações de ABI e diagnósticos para deko3d. Os quatro arquivos
nativos foram compilados da cópia privada. Objetos do guest e bibliotecas já
compilados foram copiados para `outputs/geometry-resident/link-inputs` e usados
num link separado. EXE e DLLs ficam em`bin/`, sem escrever no build do fork.
Os headers do SDK ainda são os locais: esta pasta não é um checkout completo
autossuficiente de todas as dependências.

`build-private.cmd` chama o compilador via ambiente VS; `build-private.py`
preserva manifesto dos objetos/bibliotecas usados. A primeira ligação detectou
o símbolo de geometry shader com ABI diferente no objeto do outro chat; a
correção foi compilar também os demais arquivos nativos do snapshot privado.

## Verificações

1. **Alocador:** fronteiras, limite de memória, coalescing e20000operações
   aleatórias com aposentadoria e conclusão simuladas. Asserts ativos, não
   suprimidos por`NDEBUG`. Nenhuma sobreposição entre blocos vivos/pendentes.
2. **Integração:** dois replays de150s, mesmo EXE e DLLs, flags0/32. Ambos
   chegaram à fase e encerraram com código0. Capturas do quadro3000 tiveram
   sete buffers transitórios por sessão conferidos contra a própria fonte.
   Nenhum evento de timeline perdido ou linha de quadro incompleta.
3. **Leitura dirigida:** as capturas programadas não coincidiram com uso de
   blocos residentes. Por isso foi feita uma execução diagnóstica separada,
   de110s, com leitura dos primeiros blocos admitidos e reutilizados. Os oito
   dumps residentes somaram **10.930.000bytes**, todos idênticos à própria
   fonte. Saída0, sem `vkQueueSubmit failed`, `VK_ERROR_DEVICE_LOST` ou erro
   crítico. Essa execução tem instrumentação adicional e não é benchmark.
4. **Imagens:** comparação de tela inicial e gameplay sem regressão aparente.
   O replay é por tempo; poses, partículas e instantes da cena não são iguais
   entre sessões. Não alegar igualdade integral de pixels entre execuções.

## Medidas PC

Mesmo executável nas duas rodadas de desempenho:
`200C7D02A0986F3B77A64FF3472AA874CC4D148BCE10816D5243CB0880ACC825`.
Essa versão foi preservada em`bin-metrics/` antes do build de leitura dirigida.
Filtro: quadros>=3050, pelo menos400draws; exclui a captura do quadro3000.

| Medida | Padrão | Residente32MiB |
|---|---:|---:|
| Quadros selecionados | 774 | 851 |
| Intervalo p50 | 33,3105ms | 33,3016ms |
| Intervalo p95 | 34,6177ms | 34,6850ms |
| Intervalo p99 | 35,0696ms | 46,9676ms |
| Renderer wall p95 | 10,1753ms | 10,1307ms |
| Soma durações GPU p50 | 9,3628ms | 8,2669ms |
| Soma durações GPU p95 | 10,1031ms | 8,9593ms |

O escopo GPU foi menor nessa rodada, mas o replay não fixa o mesmo instante de
cena e o PC não reproduz o Tegra. O intervalo p95 praticamente igual e p99 pior
não sustentam anunciar ganho global. CPU wall e durações GPU não são aditivos;
as durações das submissões não são uma timeline calibrada de ocupação GPU.

Último contador da sessão no quadro3900:112hits,16.277.664bytes enviados ao
residente,4.743.336.828bytes percorridos por hashing,2099alterações de conteúdo,
6382fallbacks,0evicções,16.842.752bytes ocupados de33.554.432. Hits contam
consultas, não uploads completos que teriam ocorrido no padrão. O fallback
atual reúne limite de identidades e falta de bloco contíguo; não é prova de
OOM físico nem mede separadamente essas duas causas.

## O que isso muda no próximo passo

A infraestrutura de residência/fences e sua leitura de volta foram exercitadas,
mas a primeira política beneficia poucos buffers integrais estáveis. Muitos
dos pools grandes variam e ficam fora dela. Aumentar apenas o orçamento não
elimina essas alterações nem os limites da admissão.

Próxima evolução precisa separar os motivos de fallback, reciclar metadados
antigos e estudar atualização por trechos/versionamento dos buffers dinâmicos.
Também pode avaliar residência de faixas estáticas menores, preservando as
provas de limites de fetch. Não promover esta versão como correção de FPS no
Switch com base somente nos números do PC.

Evidências: `outputs/geometry-resident/comparison.json`,
`resident-readback-proof.json`, `paired-scenes.png`, logs de build, snapshots
e manifestos. Código compartilhado do renderer/SDK não foi alterado; não houve
publicação no GitHub nem teste de hardware nesta rodada.
