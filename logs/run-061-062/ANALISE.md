# Execuções 061/062 — diagnóstico da captura

## Fatos confirmados

- 061 usa o renderer nativo. A captura foi armada no quadro 462.
  Gravou dois snapshots de entradas de draw e nenhuma imagem de resolve.
  O log termina depois do draw 1. A pasta `native` contém exatamente os seis
  arquivos desses dois snapshots.
- 062 usa FSI. Foram duas solicitações: quadros 314 e 339.
  Cada uma gravou 51 resolves e 93 snapshots distintos de entradas.
  As duas pastas completas foram recebidas e preservadas localmente.
- Os 79 pares `pass_f629`/`pass_f1919` têm o schema antigo, sem backend,
  registradores e retângulo de cópia. Não são as imagens da captura 061.
- O `rex_crash (3).log` é cumulativo. Só o último evento foi usado para
  correlacionar esta falha; não atribuir crashes históricos à execução atual.

## Falha localizada

ELF utilizado: `switch/artifacts/green_lantern_native_capture.elf`, preservado.
`addr2line` e disassembly localizam PC `imagen+0x1E310BC` em
`NativeGraphicsSystem::WriteMmioThunk`. Instrução `str w3,[x5,#1812]`, dentro da
atualização de registradores. O ponteiro x5 é `0x10E5A32B60`; o destino é
`0x10E5A33274`, exatamente o FAR registrado. A página tem permissão `0x1`
(somente leitura); ESR indica write permission fault.

O helper da captura passava `regs.values` diretamente para `fwrite` enquanto
threads do guest continuavam atualizando MMIO. A proteção temporária das páginas
fonte pelo IPC de filesystem no Horizon é uma explicação compatível com os
endereços e o instante da falha. O log não registra a chamada que mudou a
permissão, portanto não declarar essa cadeia causal definitivamente provada.
Não há evidência neste evento de OOM ou de erro de alocação de readback Vulkan.

## Correção preparada

`WriteCaptureSnapshot` cria uma cópia com páginas próprias, alinhadas a 4 KiB,
com tamanho arredondado ao múltiplo de página, antes da gravação. O arquivo usa
I/O sem buffering para manter a fonte dentro dessas páginas isoladas. A cópia
é liberada após fechar o arquivo. Falha de alocação é reportada sem abortar o
jogo. Snapshot de registradores custa aproximadamente 84 KiB temporários.

Aplicado aos snapshots de registradores e system constants dos draws, aos
registradores dos resolves FSI e dos dumps nativos. Os pixels de readback
continuam no buffer dedicado da GPU, com sincronização existente.
Isto evita emprestar memória viva ao filesystem; não torna a captura um replay
atômico de todas as threads nem altera a semântica de MMIO do jogo.

Compilados ambos os targets e NROs, código de saída 0:

- `green_lantern_native_capture_isolated.nro` / ELF — próximo a executar.
- `green_lantern_fsi_capture_isolated.nro` / ELF — disponível, sem necessidade
  de repetir FSI nesta rodada.

Log de build: `outputs/switch-fsi/build-capture-isolated.txt`.
Hashes: `capture-isolated-artifacts.json`. Versões v5 anteriores preservadas.
Correção ainda não confirmada no Switch.

## Comparação disponível

Os dados novos foram organizados em `capture/native` e `capture/fsi`.
O decodificador produziu previews e `decoded/comparison.json` dos destinos
FSI nos formatos implementados, recusando depth/array e o formato Xenos 7.
Não há pares de pixels novos entre os renderers: o nativo não os gravou.

Nos dois draws nativos disponíveis há candidatos FSI com os mesmos hashes VS/PS
e zero diferenças no bloco de render state `0x2000..0x23FF`. Existem diferenças
em fetch e constantes float; parte dos fetches é inativa nesses draws e há
endereços de vértices específicos da sessão. Isso não demonstra erro de shader
nem igualdade das entradas do quadro inteiro. Não inferir correção visual a
partir desses dois draws iniciais.

## Única próxima ação de hardware

Abrir `green_lantern_native_capture_isolated.nro`, chegar ao mesmo menu e
pressionar L+R+Menos uma vez. Enviar `capture/native`, log numerado e crash
atualizado se falhar. Não precisa repetir FSI.

Se completar, comparar os destinos e entradas com o FSI já recebido. Se falhar
na mesma página, reavaliar a origem da proteção; se falhar em GPU/readback,
tratar o novo ponto com o ELF e log correspondentes. Não prometer correção de
FPS ou imagem por esta alteração de diagnóstico.
