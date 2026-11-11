# Protótipo deko3d

O snapshot de `project/switch` desta pasta complementa a árvore principal. Copie-o sobre o projeto para recuperar os pontos de integração deko3d. O alvo `green_lantern_switch_deko` exige `GREEN_LANTERN_BUILD_DEKO_VARIANT=ON` e o renderer nativo ligado. O CMake espera `deko3d-sdk-reference` como diretório irmão da raiz que contém `project`. Ajuste o caminho `_deko_sdk` ao montar outra estrutura. Os dois arquivos do núcleo deko3d necessários a esse alvo estão nesta pasta, com a licença do nfsmw-nx preservada.

Os scripts de `deko_experiment` registram a conversão AOT e o empacotamento. Gere o corpus local com shaders da sua cópia do jogo. O runtime procura `sdmc:/switch/green_lantern/deko_shaders/modules.txt` e os módulos correspondentes. O relatório descreve os layouts, compiladores usados, limitações e resultados do hardware.

- [Relatório até o teste 085](../../docs/RELATORIO_DEKO3D_ATE_085.md)
- [Logs e análises de hardware](../../logs/deko3d/)
- [Registro 090 do deko3d v9](../../logs/run-090/RESULTADO_090.md)
