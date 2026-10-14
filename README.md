# Green Lantern: Rise of the Manhunters — pesquisa do port para Switch

Port experimental da versão de Xbox 360 para Nintendo Switch, usando ReXGlue, libnx e Mesa/NVK.
A recompilação para Windows já foi jogada localmente. No Switch, a versão atual inicializa o Vulkan,
a cadeia de apresentação de imagens (swapchain) e um renderizador nativo experimental, mas
**ainda não exibiu nenhum quadro do jogo**.

Este repositório reúne uma cópia dos fontes para revisão por outros desenvolvedores de ports.
Inclui as adaptações de plataforma, o renderizador nativo, as definições de compilação, o manifesto
de análise e os logs recentes do console. Os executáveis, recursos e saves do jogo, os fontes
gerados pela recompilação e os binários NRO ficam fora deste repositório.

## Por onde começar

- [Bloqueio atual, evidências e perguntas para revisão](docs/REVIEW_NOTES.md)
- [Último log do console: 035](logs/green_lantern_035.log)
- [Inicialização do sistema gráfico nativo](project/switch/native_experiment/native/native_system.cpp)
- [Reserva antecipada das pilhas das threads gráficas](sdk-overlay/src/core/threading_switch.cpp)
- [Erros originais do libnx e uso das pilhas reservadas](sdk-overlay/src/ui/switch_perf.cpp)
- [Alocador da memória de suporte do Xbox 360](sdk-overlay/src/core/guest_memory_switch.cpp)
- [Patch do SDK](patches/rexglue-sdk.patch) e [revisão usada como base](SDK_BASE.txt)

## Organização dos fontes

`sdk-overlay/` contém cópias completas dos arquivos alterados e adicionados sobre o commit
`c94f5ebdcb3c9d1a460ca48e04f9758448f8d518` do ReXGlue. É possível ler os arquivos diretamente ou
copiá-los sobre uma cópia do SDK nessa revisão. O patch contém apenas as alterações dos arquivos
que já eram rastreados pelo Git; a pasta também inclui os arquivos novos.

`project/` contém a integração do Green Lantern e os fontes do renderizador e do runtime do Switch.
Este repositório depende de arquivos externos para compilar: o C++ gerado precisa ser produzido
localmente a partir dos arquivos de Xbox 360 do usuário, e as dependências do SDK e do driver
precisam ser fornecidas. Consulte as [notas de compilação e origem](docs/BUILD_AND_PROVENANCE.md).

## Licenças e créditos

A adaptação do renderizador nativo veio de **The Simpsons Game NX**, preservando os avisos
GPL-3.0-or-later nos cabeçalhos e o texto da licença em [LICENSE](LICENSE). As partes derivadas de
ReXGlue/Xenia preservam os avisos BSD. O código de plataforma do ReXGlue mantém sua
[licença BSD-3-Clause](LICENSES/ReXGlue-BSD-3-Clause.txt). Os cabeçalhos de terceiros conservam os
avisos originais. Este projeto não reivindica a autoria dos ports de referência nem do jogo original.

Os textos das licenças, os identificadores do código e as mensagens dos logs conservam a forma
original para preservar os avisos legais e permitir a busca exata dos erros.

## Estado atual em linguagem simples

O jogo ainda não mostrou imagem no Switch. A última versão falhou ao iniciar duas tarefas gráficas,
mesmo com folga nos limites gerais do processo. O novo NRO reserva espaço para essas tarefas antes
da inicialização gráfica e registra o erro original se falharem. Ele compilou, mas ainda aguarda teste
no console. Os arquivos deste repositório permitem que outro desenvolvedor revise esse trabalho.
