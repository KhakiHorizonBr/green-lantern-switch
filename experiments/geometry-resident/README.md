# Geometria residente — experimento Vulkan, não promovido

Fontes de uma cópia isolada do renderer Vulkan, testada no PC em29/10/2026.
O renderer ativo do projeto não foi substituído por este experimento.

Leia [a avaliação](../../docs/FPS_RESIDENT_GEOMETRY.md) e
[as evidências](../../logs/pc-resident-geometry/comparison.json) antes de usar.
Flag registrada somente nesta cópia: `nx_geometry_resident_mib` (`0` padrão;
`32` divide o buffer em128MiB transitórios +32MiB residentes).

`native/` contém o snapshot completo usado na variante; não apenas uma classe
de cache. Compilar seus quatro arquivos CPP juntos evita misturar ABIs de
geometry shader com os objetos de outro renderer. Dependências, SDK e código
gerado do guest continuam externos, conforme a documentação geral do projeto.
O build local usou objetos do guest/bibliotecas preservados e um link privado;
o script local de importação depende do layout daquela máquina e não é um
instalador portátil distribuído aqui.

O fonte publicado inclui o modo de leitura dirigida, condicionado à opção de
verificação de vértices no PC. O JSON de desempenho corresponde ao build anterior
à leitura dirigida; a diferença é instrumentação limitada de readback, não a
política de cache. Não comparar timings da execução de verificação com os benchmarks.

O alocador de páginas/fences pode ser verificado separadamente, com asserts ativos:

```sh
c++ -std=c++20 -O2 check-pages.cpp -o check-pages
./check-pages
```

No MSVC, usar `cl /EHsc /std:c++20 check-pages.cpp` num prompt de desenvolvimento.
Não definir `NDEBUG` para esse teste. Ele faz20000operações aleatórias e verifica
que blocos vivos ou ainda usados por submissões não se sobrepõem.

Resultado: buffers conferidos corretamente, mas nenhum ganho claro de FPS PC.
A política inicial tem baixo reaproveitamento dos buffers integrais dinâmicos.
Permanece desligada. Nenhum NRO é distribuído nem houve teste Switch desta variante.
