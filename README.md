# Green Lantern: Rise of the Manhunters — Switch port research

Experimental Xbox 360 to Nintendo Switch port using ReXGlue, libnx and Mesa/NVK.
The Windows recompilation has been played locally. The Switch build currently initializes Vulkan,
the swapchain and an experimental native renderer, but has **not displayed a game frame**.

This is a source review snapshot for other port developers. It includes the platform changes,
native renderer adaptation, build definitions, analysis manifest and recent hardware logs.
Game executables, assets, saves, generated game recompilation sources and NRO binaries are omitted.

## Where to start

- [Current blocker, evidence and questions](docs/REVIEW_NOTES.md)
- [Latest hardware log: 035](logs/green_lantern_035.log)
- [Native graphics startup](project/switch/native_experiment/native/native_system.cpp)
- [Early native-worker stack reserve](sdk-overlay/src/core/threading_switch.cpp)
- [Raw libnx failure reporting and reserved-stack handoff](sdk-overlay/src/ui/switch_perf.cpp)
- [Guest backing allocator](sdk-overlay/src/core/guest_memory_switch.cpp)
- [SDK patch](patches/rexglue-sdk.patch) and [base revision](SDK_BASE.txt)

## Source layout

`sdk-overlay/` contains complete copies of changed and added files against ReXGlue commit
`c94f5ebdcb3c9d1a460ca48e04f9758448f8d518`. They are easy to read directly and can be copied onto that
checkout. The patch contains tracked-file edits only; the overlay also contains newly added files.

`project/` contains the Green Lantern integration and the Switch renderer/runtime sources.
This repository is not a standalone build: generated C++ must be produced locally from the user's
own Xbox 360 files, and the external SDK/driver dependencies must be supplied.
See [build and provenance notes](docs/BUILD_AND_PROVENANCE.md).

## Licences and attribution

The native renderer adaptation comes from **The Simpsons Game NX**, retaining GPL-3.0-or-later
headers and its licence text in [LICENSE](LICENSE). Its ReXGlue/Xenia-derived portions retain
their BSD notices. ReXGlue platform code retains [BSD-3-Clause](LICENSES/ReXGlue-BSD-3-Clause.txt).
Third-party source headers retain their original notices. This snapshot does not claim ownership
of the upstream ports or of the original game.

## Para quem acompanha em português

O jogo ainda não mostrou imagem no Switch. A última versão falhou ao iniciar duas tarefas gráficas,
mesmo com folga nos limites gerais do processo. O novo NRO reserva espaço para essas tarefas antes
da inicialização gráfica e registra o erro original se falharem. Ele compilou, mas ainda aguarda teste
no console. Os arquivos desta pasta permitem que outro desenvolvedor revise exatamente esse trabalho.
