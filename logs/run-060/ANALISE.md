# Execução 060 e avaliação das sugestões recebidas

## O pedido de 540p não foi adotado

FSI v4 confirma video_mode_width=960/video_mode_height=540 aplicados, mas os quatro
primeiros swaps reais são1280×720. Não há mudança posterior de dimensões registrada.
No último intervalo,782 de850draws/frame têm largura1280–1599. Portanto a variante
não demonstra redução do trabalho interno e NÃO permite concluir que reduzir
resolução é ineficaz. O jogo mantém alvos internos próprios; localizar esse ajuste
no jogo seria necessário antes de repetir um teste de resolução.

Último intervalo:1,5FPS,0misses/0compilações,configure10,331ms,draw8749,574ms,
resolve22,259ms,end_submission110,809ms,15fence waits/8194,329ms em10054,254ms.
SpanGPU393,390ms (inclui possíveis lacunas). Leituras emuladas continuam0.
Não interpretar esses timers inclusivos como parcelas somáveis.

Malloc reporta1729,61MiB usados e9,65MiB livres no final, com heap reservada2000MiB.
Isso descreve o arena atual do malloc, não necessariamente todo o tail ainda
expansível da heap nem todas as alocações do driver. Não concluir esgotamento por
esse contador sozinho. Não houve nova evidência recebida de falha de alocação que
explique o FPS aquecido; a espera por submissões continua predominante.

## Conferência das sugestões do Claude contra o código

1. FSI como referência para corrigir o rápido: adequado e agora a prioridade.
   Não integrar todo o FSI ao renderer nativo sem localizar a divergência.
2. Caminho host/framebuffers existe: render_target_path_vulkan=fbo (ou padrão).
   Já foi testado no PC e ficou preto; FSI e D3D12 RTV ficaram corretos. Não repetir
   isso como descoberta pendente. Faltam capturas comparáveis das operações erradas.
3. Ambos usam SpirvShaderTranslator do ReXGlue. O nativo o constrói com FSI=false,
   enquanto o backend FSI habilita o modo correspondente. Compartilhar a base NÃO
   significa SPIR-V idêntico: modos, constantes, bindings e saída de framebuffer
   diferem. Shaders não podem ser absolvidos inteiramente só pelo sucesso do FSI.
4. Cache em disco: o SDK armazena shaders/descrições; a chamada de criação Vulkan
   ainda usa VK_NULL_HANDLE para o cache do driver. Um VkPipelineCache serializado
   não foi implementado. É falsa a inferência de que cache aquecido em RAM prova
   existência de cache de driver persistido.
5. Pipelines já têm tempos de criação por chave/VS/PS no log. Tradução e módulos
   têm tempo agregado. Timestamps atuais são spans por frame, não por categoria.
6. MSAA1x requer coerência de layout/resolve da EDRAM; mudar só a contagem Vulkan
   não é uma otimização segura. SampleInterlock é escolhido quando disponível;
   PixelInterlock ordena mais fragmentos sobrepostos, não é universalmente mais barato.
7. Menu tem animação. Diff entre sessões precisa conferir entradas/hashes/layouts,
   não tratar qualquer pixel diferente como causa. Primeiro resolve diferente pode
   ser só consequência de um draw anterior, ou de estados/tempo não pareados.

## Trabalho que pode ser aproveitado imediatamente

O renderer nativo já captura resolves e metadados por atalho. O backend completo
tem readback seletivo por gpu_compare_resolve_address, usado no PC. Unificar a
solicitação e os metadados entre esses caminhos é uma extensão dessas ferramentas,
não construir um novo renderer.

Captura comum deve identificar: backend/frame/pass, endereço físico canônico,
retângulo, pitch, formato/endian, VS/PS e hashes das entradas relevantes. Preservar
raw e layout, converter os dois para representação linear equivalente no PC antes
de comparar. Nativo captura imagens host; FSI resolve para layout tiled do guest:
comparar esses binários diretamente seria um falso diagnóstico. Manter HDR/sinais
até depois da comparação; PNG de8bits sozinho pode esconder divergências.

Parear por recurso/operação/entradas, não pelo ordinal global de draws. Limitar
capturas por bytes e número, respeitar fences e não fazer writeback ao guest.
Manter os caminhos em NROs separados inicialmente é suficiente para comparar;
um único NRO selecionável é conveniência, não pré-requisito para achar o defeito.

As novas sugestões foram avaliadas; nenhuma foi executada como instrução externa
automática. Captura unificada ainda NÃO implementada nesta análise; não anunciar
correção gráfica, ganho de desempenho ou NRO novo.
