# Green Lantern: Rise of the Manhunters — port experimental para Switch

Recompilação da versão Xbox 360 usando ReXGlue, libnx e Mesa/NVK.
**O Switch já chegou ao gameplay com imagem correta nos testes do caminho Vulkan FSI.**
O desempenho desse caminho ainda é muito baixo. O renderer adaptado dos Simpsons
é mais rápido, mas mantém defeitos visuais. Não é um port concluído.

## Estado em 16/10/2026 — execução 059

- Comparação no PC: D3D12 correto, Vulkan host com materiais pretos e Vulkan FSI correto.
- Variante separada Xenos/Vulkan FSI chega ao gameplay sem defeitos visuais observados
  no teste 058; estabilidade de todas as fases ainda não demonstrada.
- Leituras diretas apenas do backing CPU não físico eliminaram as leituras emuladas
  registradas no perfil, preservando a vigilância dos aliases físicos da GPU.
- Execução 059: 137 pipelines distintos, média de 1,28 segundo de compilação no driver.
- Mesmo com zero compilações novas, o último intervalo ficou em 1,4 FPS e gastou
  8,97 segundos esperando submissões em um intervalo de 10,05 segundos.
- Heap mantida em 2000 MiB. Mais capacidade de RAM não elimina esse custo de execução.
- Próximo candidato compilado: pedido de vídeo 960×540, com confirmação do tamanho
  efetivamente apresentado e métricas do malloc. **Teste no Switch pendente.**

## Por onde começar

- [Estado atual, evidências e próximos passos](docs/PORT_STATUS.md)
- [Como comparar PC e Switch](docs/GRAPHICS_COMPARISON.md)
- [Resultados 054–059](docs/RESULTADOS_054_059.md)
- [Teste de resolução e interpretação](docs/FSI_540P.md)
- [O que aproveitamos dos outros projetos](docs/REFERENCIAS_EXTERNAS.md)
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
