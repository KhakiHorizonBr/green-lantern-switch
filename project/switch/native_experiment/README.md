# Experimento de renderizador nativo do Green Lantern

Em 14/10/2026, o log 035 do console chega à inicialização do renderizador, mas as duas threads
auxiliares da GPU nativa falham na criação. A versão candidata atual reserva antecipadamente dois
blocos de 1088 KiB para suas pilhas, verifica os resultados da inicialização e registra os erros
originais do libnx. Ela compila com sucesso e aguarda validação no console. Este experimento do
Switch ainda não exibiu nenhum quadro do jogo.

Esta é uma adaptação experimental, exclusiva para Switch, do renderizador Vulkan nativo dos fontes
de The Simpsons Game NX fornecidos pelo usuário. Os créditos e avisos GPL-3.0-or-later do projeto
de origem foram preservados nos cabeçalhos. É uma variante de compilação separada; o renderizador
Xenos padrão e o NRO arquivado do teste de arena continuam disponíveis na pasta de trabalho local.

As substituições de pixel shaders pré-compiladas específicas de The Simpsons não se destinam ao
Green Lantern. A primeira compilação usa uma tabela vazia de shaders embutidos; a compatibilidade
dos shaders do Green Lantern depende do tradutor SPIR-V genérico do ReXGlue e será avaliada
separadamente.

A camada de compatibilidade adiciona as entradas da tabela de chamadas do dispositivo Vulkan e o
gancho opcional `VertexFetchObserver` de que esse renderizador precisa na cópia experimental do
ReXGlue. O caminho acelerado de texturas específico de The Simpsons está desativado porque seus
bits de modificação do tradutor não existem nesta versão do SDK do Green Lantern. Os contadores
opcionais de progresso da fila e de apresentação do projeto de origem também foram omitidos;
são usados o ponteiro de leitura do jogo e as estatísticas locais de apresentação.

O NRO é compilado com a arena atual de 320 MiB para a memória de suporte do jogo. A renderização
ainda não foi validada no Switch. Na pasta de trabalho local, use o NRO separado em `artifacts` e
preserve `green_lantern_switch_game.arena-320.nro` para retornar à versão anterior se necessário.
