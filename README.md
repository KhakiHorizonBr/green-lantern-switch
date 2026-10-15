# Green Lantern: Rise of the Manhunters — port experimental para Switch

Recompilação da versão Xbox 360 usando ReXGlue, libnx e Mesa/NVK.
**O Switch já chega ao menu, à primeira cena e ao início jogável.**
Ainda há erros gráficos graves e desempenho variável. Não é um port concluído.
A versão de PC foi jogada localmente e serve de referência visual.

## Estado em 15/10/2026

- A heap de 2000 MiB permitiu passar dos crashes de memória observados com 1280 MiB.
- Arena de backing de 448 MiB, ring de upload de 160 MiB e política adaptativa inicial.
- Correções de preservação SIMD nas exceções, ABI, pilhas das threads e cache de pipelines.
- Corrigidas omissões de formato e escala de cor nos resolves; o teste 052 teve pouca melhora visual.
- Prioridade atual: localizar a primeira divergência gráfica comparando passes com o PC.
- Novo diagnóstico por **L + R + −** salva um quadro de resolves e metadados dos desenhos.
  Compilou; a captura ainda precisa ser validada no console.

## Por onde começar

- [Estado atual, evidências e próximos passos](docs/PORT_STATUS.md)
- [Como comparar PC e Switch](docs/GRAPHICS_COMPARISON.md)
- [Mudanças desde a revisão 035](docs/CHANGELOG.md)
- [Compilação, dependências e origem](docs/BUILD_AND_PROVENANCE.md)
- [Execução 052 e métricas](logs/run-052/)
- [Renderizador nativo](project/switch/native_experiment/native/native_renderer.cpp)
- [Alocador de backing](sdk-overlay/src/core/guest_memory_switch.cpp)
- [Tratamento das exceções](sdk-overlay/src/core/exception_handler_switch.cpp)

## Conteúdo

`sdk-overlay/` contém as versões completas dos arquivos alterados e novos sobre a revisão
indicada em [SDK_BASE.txt](SDK_BASE.txt). O [patch](patches/rexglue-sdk.patch) contém alterações
dos arquivos rastreados; a pasta também fornece os novos arquivos necessários.

`project/` contém a integração e o renderer nativo. Dependências, arquivos originais do jogo
e C++ gerado pela recompilação precisam ser fornecidos localmente para compilar.
NROs, executáveis do jogo, recursos, saves, caches e capturas gráficas não são distribuídos aqui.
Os logs publicados são registros textuais de diagnóstico com caminhos pessoais removidos.

## Licenças e créditos

Renderer adaptado de **The Simpsons Game NX**, com referências ao **Project8 Switch**,
preservando GPL-3.0-or-later e os avisos BSD das partes ReXGlue/Xenia.
O estudo do KisakCOD orientou a comparação de arquitetura e orçamento, sem importar seus shaders.
Veja [LICENSE](LICENSE), [LICENSES](LICENSES/) e as referências nas notas de compilação.
Mensagens dos logs, identificadores e avisos legais mantêm a forma original.
