# Comparação gráfica com o PC

## Objetivo

Localizar o primeiro passe divergente, usando o menu como cena inicial reproduzível.
Fotos de tela demonstram os sintomas, mas não mostram qual textura/passe já veio incorreto.
O número do frame ou do draw pode diferir entre backends: comparar finalidade, recursos e cena,
não assumir correspondência entre o draw N do PC e o draw N do Switch.

## Switch

1. Usar o candidato local `green_lantern_native_renderer_graphics_capture.nro` com o INI atual.
2. Abrir em modo aplicação pelo jogo com R, como nas execuções recentes.
3. Esperar o menu estabilizar e pressionar **L + R + −** juntos uma vez.
4. Aguardar a pausa de gravação terminar antes de encerrar normalmente.
5. Copiar `sdmc:/switch/green_lantern/logs/capture/` e o log numerado da mesma sessão.

Há até quatro capturas por sessão; cada pedido captura o próximo quadro após um swap.
Os botões também continuam sendo enviados ao jogo: escolher o menu, sem ações sensíveis.
Não altera o número normal de threads de gravação; os dumps automáticos por draw continuam
desativados. Origens/destinos de resolve e metadados de draws são a primeira cobertura.
O limite de 64 imagens/64 MiB pode truncar quadros grandes; isso fica registrado no log.
Sessões posteriores podem sobrescrever nomes iguais; copiar a pasta antes do próximo ensaio.

Cada imagem tem `.bin` (texels originais) e `.json` (dimensões, formato Vulkan, tamanho/stride).
Nome com endereço OR 0x80000000 identifica a origem anterior à conversão de cor;
endereço físico normal identifica o destino. O ordinal rNN liga o par ao resolve no log.
Não confundir esse bit de marcação com endereço real do guest.

No PC, o script `project/switch/tools/decode-graphics-capture.py PASTA` usa Pillow para gerar
PNGs e `capture-summary.json`. Os números são amostrados, não estatísticas exaustivas.
Os PNGs recortam valores para 0–1 apenas na visualização; os arquivos brutos preservam HDR.
O formato antigo de preview tratava certos floats/SNORM como RGBA8: não usar isso como referência.

## PC

O executável Windows local funcional é `green_lantern_next.exe`, com Xenos/D3D12.
Não foi substituído pelo renderer nativo nem recompilado nesta etapa.

Usar RenderDoc: Launch Application, selecionar o EXE e sua pasta de trabalho habitual,
iniciar o jogo e capturar o menu com **F12**. Salvar a captura `.rdc` localmente.
Documentação oficial: https://github.com/baldurk/renderdoc/blob/v1.x/docs/getting_started/quick_start.rst

Ao analisar, procurar o draw do personagem e seguir as texturas de iluminação/composição
até os passes que as escreveram. No Switch, usar hashes VS/PS, endereços, formatos,
retângulos, pitch, MSAA e registros blend/depth do log para associar os mesmos usos.
O replay do RenderDoc depende da API e do ambiente PC; não é uma captura RenderDoc do Switch.
Ainda não há captura PC coletada nesta etapa nem diferença numérica entre os dois backends.

## Como interpretar

- Personagem já preto no passe de origem: investigar material/iluminação e recursos usados.
- Origem correta, resultado do resolve incorreto: conversão/escala/limites da cópia.
- Imagens intermediárias corretas, final errado: blend, profundidade ou composição seguinte.
- Recursos reutilizados com dimensões diferentes: verificar alias/cache e coordenadas.

Capturas contêm imagens/recursos do jogo: manter locais, sem adicioná-las ao repositório público.
