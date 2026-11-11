# Execução 058 — FSI v2, CPU-only mapped reads

## Resultado

Log identifica v2, cpu_readonly_mapped=true, readonly_mapped global=false,
requested=fsi selected=fsi fsi_supported=true. Usuário alcançou gameplay sem bugs
visuais observados e sem crash durante esta sessão. Isso não prova estabilidade
de todas as fases nem que o crash 057 tenha sido corrigido.

33 cabeçalhos de perfil, incluindo arranque: todas as taxas de leituras emuladas
são 0. A adaptação CPU-only removeu o custo registrado de dezenas de milhares
de exceções/s da sessão 057, preservando watches físicas e aliases. Evitar dizer
que o gargalo restante ainda é aquele mesmo mecanismo de leitura.

32 intervalos após arranque: FPS de 0 a 12,7. Intervalos iniciais 12,1/12,7;
últimos 0–0,7. Não associar automaticamente cada intervalo a uma cena sem marcador.
Último intervalo: CPU total 287% de 300% disponíveis, eGraphicsRender 90,3%,
GPU Commands 86%, outros workers 57,5% e 41,3%. Amostragem de PC desligada;
não é possível identificar quais funções internas dominam cada thread.
GPU Commands é uma thread de CPU, sua utilização NÃO é utilização física da GPU.
Clocks portátil: CPU1020/GPU460,8/RAM1331,2 MHz.

O log continua criando pipelines perto do fim. No último intervalo, 7 criações
para aproximadamente 5 frames, com 579 draws/frame, 41,4 resolves/frame e
20,69 MB/frame enviados à memória compartilhada. Compilação síncrona é candidata
concreta a pausas, mas falta medir duração/cache hit e separar cena fria/aquecida;
não atribuir toda a lentidão ao custo de FSI ou ao compilador sem essa medição.

## O que é esta variante

Existem dois caminhos: renderer Vulkan adaptado dos Simpsons (mais rápido,
defeitos visuais ainda não resolvidos) e backend completo Xenos/Vulkan FSI
(imagem correta no teste, lento). A variante v2 já é a segunda alternativa.
FSI é um mecanismo de ordenação de acesso por fragmentos, usado pelo caminho
Xenos para implementar packing/blending/depth/EDRAM nos shaders; não é apenas
uma opção visual que possa ser ligada no renderer nativo para corrigir materiais.

Um caminho híbrido é tecnicamente possível, mas precisa integrar operações de
EDRAM, formatos, sincronização e resolves; reaproveitar o backend completo
indiscriminadamente pode trazer seu custo junto. A comparação correta/rápida
serve de referência para transferir somente o comportamento que falta no nativo.

## Próxima prioridade técnica

Medir tempo de tradução/compilação de pipelines e cache hits, separar primeira
passagem de uma repetição com cache, então medir submissão/espera GPU e CPU.
Manter este v2 e seu ELF como referência visual. Nenhum novo NRO necessário
apenas para responder a dúvida; não invalidar a melhoria de leituras já medida.
