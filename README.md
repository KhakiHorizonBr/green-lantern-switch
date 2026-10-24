# Green Lantern: Rise of the Manhunters — port experimental para Switch

Recompilação da versão Xbox 360 usando ReXGlue, libnx e Mesa/NVK.
**O renderer nativo chega ao gameplay e teve os defeitos gráficos principais
corrigidos, confirmados pelo usuário no teste 068. O desempenho ainda não é suficiente.**
Não é um port concluído; estabilidade e gráficos de todas as fases não foram validados.

## Estado em 24/10/2026 — hardware 074

- Menu chega a aproximadamente 24–25 FPS; fase pesada ficou em 7,3–8,8 FPS no074.
- Conversão direta de depth/stencil, exportação EDRAM sob demanda e uploads
  esparsos de vértices estão implementados, com comparações pontuais de bytes no PC.
- O074 eliminou fallback de upload esparso por orçamento de sombra, mas a economia
  de cópias ainda não demonstrou ganho global sustentado na fase pesada.
- CPU, GPU e esperas por espaço no stream continuam caros. Mais RAM sozinha não
  foi demonstrada como solução; heap de 2000 MiB e arena de 384 MiB preservadas.
- Controles por posição Xbox preparados e compilados; teste de hardware adiado
  pelo usuário até haver mais otimização.
- Próxima meta solicitada: 35–40 FPS. Ainda não atingida; o pacing atual limita
  o caminho nativo a 30 FPS mesmo em cenários leves.

## Por onde começar

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
