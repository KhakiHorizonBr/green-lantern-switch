# Compilação e origem dos fontes para revisão

Base do ReXGlue: https://github.com/rexglue/rexglue-sdk.git, no commit
`c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`.

Copie o conteúdo de `sdk-overlay/` sobre uma cópia dessa revisão, preservando os caminhos relativos.
A pasta reúne o estado completo dos arquivos para revisão; aplicar apenas
`patches/rexglue-sdk.patch` deixa de fora os arquivos novos do Switch. As dependências dos projetos
de origem são fornecidas separadamente. Os cabeçalhos de terceiros incluídos na pasta mantêm
seus avisos originais.

Os fontes do sistema gráfico e do renderizador nativo foram adaptados da cópia de The Simpsons
Game NX fornecida localmente. As substituições de pixel shaders específicas daquele jogo foram
excluídas. É usado o tradutor genérico do ReXGlue; a compatibilidade dos shaders do Green Lantern
ainda não foi validada. O observador opcional de busca de vértices (`VertexFetchObserver`) e as
adaptações da tabela de chamadas Vulkan estão na pasta do SDK. O Project8 Switch e sua arquitetura
de memória e renderização também foram estudados como referência.

## Projetos de referência

- [Project8 para Switch](https://github.com/R-drg/project8-sw)
- [The Simpsons Game NX](https://github.com/R-drg/the-simpsons-game-nx)
- [Mesa para Switch](https://github.com/danfromtico/mesa-switch)
- [KisakCOD para Switch](https://github.com/artslay/KisakCOD_nx)

## Ambiente de compilação

A compilação local usa devkitA64/libnx, um SDK estático Mesa/NVK e CMake/Ninja. O log 035 do console
informa Mesa 26.2.3 (git-d4a00ea0ab), NVK GM20B e API Vulkan 1.4.354 para a instância e o dispositivo.
Os caminhos das ferramentas e as variáveis de ambiente são configuração local.

O arquivo CMake conserva a organização relativa das pastas de trabalho. Para reproduzir a
compilação completa localmente, é necessário colocar o SDK modificado no caminho esperado por
`project/switch/generated_full/CMakeLists.txt`, ou ajustar `REXGLUE_SDK_ROOT`, e gerar
`project/generated/hinted/sources.cmake` localmente. Use o manifesto de análise com seus próprios
executáveis e recursos de Xbox 360 obtidos legalmente e mantidos na máquina local.

Esta cópia não inclui os fontes gerados do jogo nem todas as alterações históricas da versão
Windows. O repositório serve à revisão do bloqueio de plataforma e renderização; ele não oferece
uma reprodução completa, com um único comando, da versão funcional de Windows ou de todos os
testes anteriores.

## Opções da versão atual para teste

```text
GREEN_LANTERN_NATIVE_RENDERER=ON
GREEN_LANTERN_SWITCH_BACKING_ARENA_MIB=320
GREEN_LANTERN_SWITCH_STREAMDATA_TEST_CAP_MIB=32
REXGLUE_PLATFORM_SWITCH=ON
REXGLUE_USE_VULKAN=ON
```

A heap fornecida pelo carregador é reduzida para 1280 MiB quando o modo de aplicação completa
entrega um valor maior. A memória de suporte do jogo e as pilhas das threads gráficas nativas são
reservadas antes da inicialização do NVK e do runtime.

## Arquivos mantidos fora do repositório

Executáveis e recursos do jogo de Xbox 360/PC, funções geradas pela tradução de PPC para C++,
caches de shaders extraídos de jogos, saves, bibliotecas compiladas do SDK e do driver, pastas
de compilação e NROs.
