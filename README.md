# Green Lantern: Rise of the Manhunters — port experimental para Switch

Recompilação da versão Xbox 360 usando ReXGlue, libnx e Mesa/NVK.
**O renderer nativo chega ao gameplay e teve os defeitos gráficos principais
corrigidos, confirmados pelo usuário no teste 068. O desempenho ainda não é suficiente.**
Não é um port concluído; estabilidade e gráficos de todas as fases não foram validados.

## Encerramento do desenvolvimento local — 11/11/2026

Infelizmente, não consegui continuar o desenvolvimento por questões pessoais, mas deixei tudo aqui para quem quiser continuar. Boa sorte!

**O renderer Vulkan/NVK foi consideravelmente mais estável nos testes do Switch e é a base recomendada para retomar o port.** No teste 088, chegou a **29,8 FPS em uma janela de cerca de 10 segundos**. Nos trechos mais pesados, houve janelas prolongadas de 7–9 FPS. [Medições do teste 088](logs/run-088/RESULTADO_088.md)

**O deko3d foi um protótipo.** Chegou ao gameplay no teste 085, mas as janelas pesadas ficaram perto de 2 FPS, com custo alto de sincronização e variantes de shaders ainda ausentes. [Relatório do protótipo deko3d](docs/RELATORIO_DEKO3D_ATE_085.md)

O acervo inclui os dois renderers, overlays de dependências, diagnósticos e experimentos até o teste 092. Para retomar o Vulkan, use 100% de resolução interna e submissões parciais desativadas. A escala de 75% não apresentou ganho sustentado.

**[Guia de retomada, cronologia e dependências](docs/RETOMADA.md)**

## Por onde começar

- [Resultados das rodadas de27–29/10](docs/RESULTADOS_2026_10_27_29.md)
- [Pesquisa dos outros ports Xbox360](docs/PORTS_X360_AUDIT_2026-10-28.md)
- [Resultados 071–074, validação e limitações](docs/RESULTADOS_071_074.md)
- [Estado e histórico atual](docs/PORT_STATUS.md)
- [Conversão direta de profundidade](docs/FPS_DEPTH_CACHE.md)
- [EDRAM sob demanda](docs/FPS_LAZY_EDRAM.md)
- [Upload de vértices: tentativas e custos](docs/FPS_VERTEX_DELTA.md)
- [Cache seletivo, upload sem sombra e controles](docs/FPS_VERTEX_CACHE.md)
- [Comparação gráfica entre PC e Switch](docs/GRAPHICS_COMPARISON.md)
- [Compilação e procedência](docs/BUILD_AND_PROVENANCE.md)
- [Logs do hardware 074](logs/run-074/)
- [Renderer nativo atual](project/switch/native_experiment/native/native_renderer.cpp)

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
