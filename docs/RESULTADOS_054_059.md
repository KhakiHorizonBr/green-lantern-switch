# Resultados da comparação e das variantes FSI — 16/10/2026

## 054–055: localizar a divergência gráfica

Capturas de passes do Switch mostraram atlas e fetches que exigiram investigar
resolves e views assinadas. A montagem de atlas foi executada no 055, sem melhora
visual relatada. Um candidato com view R16_SNORM para fetch assinado foi compilado,
mas não foi validado no hardware e não deve ser anunciado como solução.

A comparação passou ao PC, para separar diferenças de plataforma do renderer:

| Backend, com a mesma base atual | Resultado relatado |
|---|---|
| D3D12 RTV | Imagem correta |
| Vulkan host | Materiais pretos |
| Vulkan host aceitando fetch inválido | Materiais pretos |
| Vulkan host com resolve indireto | Materiais pretos |
| Vulkan FSI | Imagem correta, com pausas |

Microcódigo do pixel shader comparado coincide. Foram pareados 24 estados por
backend: registradores de renderização e fetches usados pelo shader coincidem;
constantes animadas não são todas idênticas. Diferenças de NDC e enums brutos
entre APIs não são, isoladamente, defeitos.

Readbacks GPU do primeiro tile conhecido de t14, R16, têm aproximadamente 99,6%
de pixels iguais no primeiro par D3D12/Vulkan host, com silhuetas coincidentes.
Isso não valida o atlas inteiro nem a amostragem. Os bytes capturados são privados
e não estão neste repositório. O preto persistiu durante a captura Vulkan.

## 057: primeira variante FSI no Switch

O driver confirmou seleção FSI. Menu correto segundo o usuário, mas muito lento.
O perfil ainda tinha dezenas de milhares de leituras emuladas por segundo.
Crash final em `sub_8233E7D8`, leitura guest `0x6F6656D0`; causa upstream desconhecida.
O arquivo de crash acumulado também continha OOM antigos, que não explicam por si
a falha dessa sessão. Nenhum retorno zero foi introduzido para mascarar esse acesso.

## 058: leituras diretas apenas da memória da CPU

Nova opção `RexGmSetCpuReadOnlyMapped`: permite manter tabelas logicamente READ
mapeadas somente quando o backing é não físico. As views físicas, inclusive o alias
virtual `0x7F...`, continuam sob vigilância de escritas. A opção global do renderer
nativo, que mapearia também essas páginas, continua desligada no FSI.

Todos os 33 cabeçalhos recebidos registram zero leituras emuladas. O usuário chegou
ao gameplay sem defeitos visuais e sem crash observado nessa sessão. Isso não prova
correção definitiva do crash 057 nem estabilidade de todas as fases. FPS ainda baixo.

## 059: medição de compilação e espera

- 137 pipelines e 137 chaves distintas, todos criados com sucesso.
- Tempo na criação pelo driver: 175,7 s somados, média 1,28 s, máximo 3,46 s.
- Último intervalo: 1,4 FPS, zero compilações novas e 12.929 hits no cache.
- Nesse intervalo de 10,05 s, as esperas por fences somam 8,97 s.
- Span GPU médio de 432 ms; pode incluir lacunas entre submissões, não é ocupação pura.
- CPU portátil: 1020 MHz; GPU: 460,8 MHz; memória: 1331,2 MHz.

Há dois problemas: pausas na compilação e custo sustentado após aquecer o cache.
Não atribuir a lentidão inteira aos shaders novos. Não somar timers inclusivos:
`draw_ms` contém chamadas de configuração e esperas; `configure_ms` inclui compilação.
`GPU Commands` é uma thread de CPU, não uma leitura da utilização física da GPU.

## Candidato seguinte

Pedido de vídeo 960×540 ao jogo, sem alterar FSI, MSAA, heap ou clocks. A alteração
só reduz trabalho se o jogo respeitar o modo solicitado. Compilado, ainda sem
resultado no Switch. Ver [FSI_540P.md](FSI_540P.md).
