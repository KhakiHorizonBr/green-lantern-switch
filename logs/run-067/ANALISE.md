## O que a análise067 realmente mostra

26pares candidatos de destinos contra FSI314/339. O produtorR16 segue próximo.
Em relação ao FSI339, erros médios de1D128000 e1BF30000 caem de0,2741/0,2869
na064 para0,0279/0,0428 na067. É uma aproximação numérica dos buffers, NÃO ganho
visual garantido: fundos dominam e personagens/câmera não estão sincronizados.

Inspeção das imagens mostra o personagem inteiro no nativo e cabeça ampliada no
FSI em vários passes. Hashes iguais de VS/PS ou copy_base não tornam essas duas
entradas idênticas. O primeiro destino também contém bytes indefinidos em áreas
não desenhadas. Passes de words/stencil podem parecer padrões coloridos sem
representar corrupção visual. Depth e formatoXenos7 não são tratados comoRGBA8
pelo decodificador. Não rotular essas diferenças todas como bugs provados.

Importações1216/p1040/f4->1280/1536/p1280/f0 realmente executam na067; o cache
continua parcial (sem transferência genérica de profundidade/HDR/MSAA). Essa
é uma lista de lacunas, não prova de que todas causam a imagem preta atual.

## Próximo passo operacional

Reproduzir menu/personagem no executável nativo do PC e comparar com o FSI do PC
no mesmo enquadramento, reunindo alterações comprovadas antes de outro NRO.
O launcher já faz screenshot a cada10s e captura completa no frame450; esse frame
é uma conveniência, não replay determinístico do menu. Ajustar nx_capture_frame
se necessário, sem pedir cópia de NRO para o Switch. Para um replay completo das
capturas antigas ainda faltam entradas de vértices/texturas/shader bytecode;
JSON/registers/system/pass images sozinhos não permitem reproduzir todo draw.

O PC permite localizar muitos erros de lógica e usar depuração Vulkan. Não
prevê bugs do NVK/Tegra, memória disponível nem FPS do Switch; uma verificação
final no console continua necessária. Não prometer teste definitivo único.

A ferramenta compare-renderer-captures.py aceita --fsi-source para consultar a
referência original sem fazer novas cópias dos dumps. A comparação067 atual
aponta para outputs/run-061-062/capture/fsi; originais preservados. Cópias idênticas
da referência067 foram removidas após conferirSHA256. Tentativa de hardlink foi
negada pelo ambiente; a ferramenta agora usa caminhos diretos.
