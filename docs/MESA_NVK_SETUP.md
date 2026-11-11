# Preparação do NVK para o port

## O que a referência confirma

O Green Lantern precisa de um backend Vulkan para a GPU do Switch. A SDK de Horizon usada como referência liga NVK estaticamente como `libvulkan.a`; o driver não é fornecido pelo homebrew/runtime do console.

O README atual de `danfromtico/mesa-switch` documenta `build-switch.sh`, que constrói somente NVK/Vulkan dentro de um container Docker e deixa as bibliotecas estáticas em `builddir-switch`. O projeto Project 8 já tem uma integração testada mais específica: fixa Mesa Switch no commit `d4a00ea0ab3` (Mesa 26.2.3), aplica os patches numerados de `patches/mesa-switch/` e executa `build-switch-p8.sh`. É mais seguro reproduzir essa combinação conhecida do que aplicar os patches do Project 8 diretamente sobre o `main` atual, que já avançou para Mesa 26.2.4.

Depois do build, o Project 8 mescla e valida os arquivos `.a` do NVK em `lib/libvulkan.a`, indexa o archive com `aarch64-none-elf-ranlib` e mantém `libnak_rs.a` no mesmo SDK. Esse diretório será passado como `REXGLUE_SWITCH_NVK_SDK` na configuração do app.

## Docker neste PC

- Instalador oficial baixado em `<USER>\Documents\Codex\downloads\Docker Desktop Installer.exe`.
- Versão: 4.94.0.241994; tamanho: 635.493.296 bytes.
- Assinatura Authenticode: válida, assinante Docker Inc.
- SHA-256 local: `A9814E31049D66156477A86614E83365669677733014EC72F74229623FF3890A`.
- O comando `wsl --version` informou que o WSL não está instalado. Portanto, antes do primeiro container Linux, será necessário instalar/atualizar o WSL 2; o Windows pode pedir elevação e reinicialização. Docker Desktop aceita instalação per-user, mas a ativação inicial do WSL pode exigir administrador.
- O PC tem 16 GB de RAM. Vamos começar o build do Mesa com paralelismo moderado e observar memória, em vez de maximizar os workers logo de início.

## Estado

O Docker apenas foi baixado e verificado; não foi instalado. O código Mesa ainda não foi clonado/construído e ainda não há `libvulkan.a` NVK local. Nenhum novo NRO foi produzido. O NRO `diag8` continua sendo a última build confirmada em hardware.

## Fontes de referência

- https://github.com/danfromtico/mesa-switch
- https://github.com/R-drg/project8-sw/blob/main/docs/building.md
- https://github.com/R-drg/project8-sw/tree/main/patches/mesa-switch
