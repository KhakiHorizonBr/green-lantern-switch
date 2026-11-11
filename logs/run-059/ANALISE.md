# Execução 059 e referências Rayman/XenosRecomp — 16/10/2026

## Resultado medido

FSI v3 confirmado, leituras CPU-only diretas mantidas. Sem access violation no log
principal recebido. Não concluir estabilidade completa a partir de uma sessão.

137 criações de pipelines, 137 chaves de estado distintas, todas VK_SUCCESS.
Tempo dentro de vkCreateGraphicsPipelines: total175.700,438ms, média1.282,485ms,
máximo3.460,194ms. Não houve repetição da mesma chave nesta sessão. Repetições de
VS/PS com chaves de estado diferentes não são prova de cache defeituoso.
Dados extraídos em summary.json. Esse total não é CPU total nem custo por frame.

Último intervalo: 1,4FPS,10.052,217ms de parede, 0misses/0compilações/0traduções,
12.929hits de pipelines; configure8,682ms, draw9.561,543ms, resolve26,545ms,
end_submission105,690ms.14esperas por fences somam8.973,450ms. Span GPU médio432,090ms
em14amostras.924draws,52,4resolves e24,49MB de upload por frame.
CPU: eGraphicsRender98,3%, GPU Commands8,4% (essa é thread CPU), workers58,8/47,8%.

Conclusão: compilação explica pausas importantes, mas NÃO explica o FPS sustentado
no último intervalo aquecido. A thread de comandos está majoritariamente esperando
conclusão de submissões. Span GPU ainda pode incluir lacunas entre submissões e
não mede ocupação/tempo de shader isolado. Não somar timers inclusivos.

## RaymanOriginsRecomp

https://github.com/XDanfr/RaymanOriginsRecomp
Main consultado: c1bf7b90e95d476e63aa4a0b077905ff6dbde9bd.

README/docs do código consultado documentam execução macOS/Android; a descrição
do fork menciona Switch, mas isso sozinho não comprova benchmark no Switch.
docs/NATIVE_RENDERER.md: intercepta chamadas D3D do jogo e usa shaders SPIR-V
antecipados, sem emulação completa do Xenos. Diferente do nosso backend FSI.
docs/ANDROID_PERFORMANCE.md: cache Vulkan persistido + lista de pipelines preparada
no startup; dois frames em voo; decoder de vídeo R8; redução de uploads por draw.
tools/native_renderer/vk_renderer.h confirma VkPipelineCache, recuperação vazia
em falha, PipelineCacheData/Prewarm e uso do cache em vkCreateGraphicsPipelines.

Aplicação: cache de driver e preparação antecipada são referências concretas para
reduzir pausas. Nosso pipeline_cache.cpp tem cache de descrições do SDK, mas passa
VK_NULL_HANDLE como cache de driver à criação Vulkan. A integração deve validar
identidade do dispositivo/driver, sincronizar acesso entre threads e preservar
os recursos até o fim das submissões. NÃO esperar que isso resolva o intervalo
aquecido de1,4FPS com0compilações. Dois frames em voo também não tornam a GPU mais
rápida por si; o nosso backend já possui submissões/frames em voo e dependências
de EDRAM/queries que não podem ser removidas indiscriminadamente.

Licença da referência: GPLv3, arquivo LICENSE consultado. Não foi copiado código.

## XenosRecomp

https://github.com/hedge-dev/XenosRecomp
README consultado: conversor de shaders360 paraHLSL, compilável porDXC emDXIL/SPIR-V.
Não é um renderer/driver/implementação de EDRAM. Projetado para UnleashedRecompiled;
o próprio projeto enumera recursos incompletos/específicos e necessidade de adaptar.
Rayman documenta correções próprias no resultado (setp_inv e constantes relativas).

Aplicação: possível base para shaders antecipados de um caminho nativo/híbrido,
com validação das instruções e formatos que GreenLantern usa. Não substitui o
pipeline FSI atual nem elimina o custo de compilar os pipelines no driver, que
precisa gerar código para a GPU alvo mesmo recebendo SPIR-V pronto.

## Prioridade após este resultado

Preservar FSI como referência correta e investigar custo GPU sustentado, não pedir
mais uma sonda só para contar pipelines. Revisar MSAA/sample shading, tráfego/formatos
de EDRAM, overdraw e resolves, sem remover passos necessários à imagem correta.
Cache/prewarm é trabalho separado para pausas. Caminho nativo/híbrido pode aproveitar
o comportamento correto do FSI nas operações que faltam, mas não é um toggle.

## Rollback

Usuário apagou do Switch o NRO anterior. Não afetou os saves/caches por si.
Cópia local v2 existe: switch/artifacts/green_lantern_xenos_fsi_cpu_reads.nro,
53.962.949bytes; ELF correspondente também preservado.
