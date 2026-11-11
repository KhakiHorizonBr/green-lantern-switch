# Experimento de renderizador nativo do Green Lantern

Em 15/10/2026, o renderer já chega ao menu e ao início jogável. Persistem erros de iluminação e composição e desempenho variável. O candidato posterior ao teste 052 acrescenta captura de passes por L+R+−; compilou e ainda aguarda validação no console.

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

A configuração local atual usa heap de 2000 MiB, arena de 448 MiB por INI e ring de 160 MiB. Os artefatos anteriores e seus ELFs permanecem locais para rollback. Consulte docs/PORT_STATUS.md no repositório público para evidências e limites atuais.

