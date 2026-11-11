# Comparação do renderer no PC — 17/10/2026

## Atualização: menu recuperado pela cópia de vértices

O resultado atual substitui as observações anteriores de corpo ainda preto.
Com `nx_vertex_fetch_safety=8` e `nx_guest_depth_range=true`, o menu nativo
mostra uniforme, símbolo, braço e rosto. Caso `vertex-safety-8`, saída0.
Não é uma substituição de material: sondas de cor e bypass de depth desligados.

| Caso | Alteração na cópia | Resultado do menu |
| --- | --- | --- |
| material-solid | Cor magenta fixa, cópia antiga | Só cabeça; geometria deslocada |
| material-solid-full-vertex | Magenta, buffers completos | Corpo inteiro |
| full-vertex-normal-render | Cópia conservadora, material normal | Uniforme correto |
| vertex-safety-5 | Revalidar draw e hash, mantendo recortes | Corpo preto |
| vertex-safety-3 | Buffers completos, mantendo cache de hashes | Uniforme correto |
| vertex-safety-8 | Completos somente para índices indiretos | Uniforme correto |

Os limites inferidos para acessos indiretos não cobrem os dados necessários
nesta cena. O novo padrão8 evita esse recorte; preserva o recorte de streams
com índice direto e o cache existente. O ramo de inferência antigo continua
disponível pelo modo0 para comparação/rollback. Não foi provado qual instrução
do interpretador calcula o limite errado, nem que todas as cenas estejam corretas.

Caso `vertex-safety-8-gameplay`: chegou à fase, cenário e personagem visíveis,
até encerrar pela automação após100s, saída0. Captura `frames/frame_18.ppm`.
Não houve movimentação prolongada nem comparação pixel a pixel com o FSI nessa fase.
Evidências privadas em `outputs/native-pc-user/`.
O orçamento de memória não foi alterado. As seções abaixo são o histórico.

## Continuação: restauração de stencil comprovada

Identificado no SDK (`render_target_cache.cpp`, shader de transferência) que cor
e depth/stencil trocam as metades de 40 amostras de cada tile de 80. A cópia
R8 → stencil do nativo ignorava essa permutação. Aplicada em regiões 1x alinhadas,
com opção `nx_stencil_color_layout=true`; `false` preserva a comparação antiga.
O shader guest remapeia a leitura, mas sua saída em cor ainda usa endereçamento
de cor, portanto a permutação é necessária na transferência para stencil.

Sonda Windows captura a imagem R8 imediatamente após o shader de restauração.
Comparada à máscara original, na mesma execução e com offsets Y 0, 256 e 512:

| Faixa | Cópia direta antiga | Com permutação |
| --- | ---: | ---: |
| Y=0, altura 256 | 97,5543% iguais | 100% iguais |
| Y=256, altura 256 | 95,0586% iguais | 100% iguais |
| Y=512, altura 208 | 90,2118% iguais | 100% iguais |

Evidência: `outputs/native-pc-user/stencil-layout-proof.json`, script
`prove-stencil-layout.py`, casos `stencil-layout` e `stencil-layout-proof`.
A permutação também foi conferida como bijetiva, involutiva e limitada a cada
tile. Shader SPIR-V e build PC compilam. As sondas extras são só Windows.

**Limite:** o menu continua com o corpo preto. A correção de stencil é comprovada,
mas não é solução completa para a iluminação. Nenhum novo teste Switch solicitado.

## Resultado atual

O renderer nativo ainda desenha o corpo preto. As mudanças desta rodada não
devem ser entregues como uma correção visual completa. Não foi gerado novo NRO.

## Correções e medições confirmadas

1. **Limpeza compartilhada de profundidade e cor.** As limpezas nas bases 1792
   e 1328 usam um Z ligeiramente negativo, que o depth clamp habilitado transforma
   em zero. O reconhecimento antigo não aceitava esse caso. Agora a base 1328
   recebe 720 tiles zeradas. No passe HDR 18, o alfa passou de 331 para zero.
   O corpo continuou preto, tanto isoladamente quanto junto da variante de depth.
2. **Faixa de profundidade.** A variante `nx_guest_depth_range=true` elimina
   o fator dois no depth medido. Isso não resolveu o material. A opção permanece
   disponível para comparação e seu padrão continua `false`.
3. **Captura FSI com dados antigos ou zerados.** Aumentar o buffer de readback
   podia fechar a submission; faltava reabri-la antes da cópia. Comandos gravados
   depois desse fechamento eram descartados no próximo reset. Corrigidos os
   caminhos de readback e de realocação do buffer de downscale. Build PC passou.
   Na repetição, o HDR anteriormente totalmente zerado passou a conter 4.894.627
   bytes não zero de 7.536.640, e a imagem decodificada mostra o material correto.

## Comparação que continua em aberto

O replay FSI recebeu o botão A aos 50 segundos para aguardar a compilação inicial.
O screenshot confirma o menu aberto e o personagem correto. A captura corrigida
é do frame 2600. A referência nativa é do frame 1200, no mesmo menu.

Nos nove snapshots do PS `09B540165140DBF6`, os registradores de renderização e
fetch coincidem entre os dois backends. Constantes de animação e ordem das luzes
variam; os frames não constituem um replay determinístico dos mesmos dados.
As faixas intermediárias inferiores continuam diferentes. A análise seguinte
precisa conferir as imagens amostradas e os valores de stencil, antes de atribuir
essas diferenças a coordenadas ou implementar transferências gerais de EDRAM.

Uma sonda adicional no PC capturou a textura exata de bytes de depth/stencil
consumida pelo shader de restauração, sem gastar o orçamento de captura do Switch.
No passe 2, o stencil contém apenas 0/1 nos dois caminhos: nativo 217.102 pixels
com valor 1, FSI 220.322. As poses são diferentes. No passe 19, ambos têm apenas
zero. Isso não prova que todos os testes de stencil intermediários estejam
corretos, mas não sustenta a hipótese de perda da máscara inicial ao empacotar
depth/stencil. Build e execução dessa sonda terminaram com saída 0.

## Evidências locais privadas

Dentro de `outputs/native-pc-user/`:

- `zero-clamped-verified/`: limpeza corrigida, faixa antiga de depth.
- `combined-depth-zero/`: limpeza corrigida e faixa guest de depth.
- `hdr-clear-metrics.json`: valores antes/depois da limpeza.
- `fsi-menu-late/`: confirma o menu correto, mas ainda usa captura com o defeito.
- `fsi-readback-fixed/`: captura e screenshot após corrigir o readback; saída 0.
- `readback-fixed-comparison/fresh-passes.png`: comparação dos passes 0 a 19.
- `compare-light-inputs.py`: comparação dos registradores dos draws de iluminação.
- `native-stencil-words/` e `stencil-histogram.json`: sonda dos bytes de stencil.

Os dados derivados do jogo permanecem locais. A automação usa cópias privadas dos
saves e não altera os originais. Os limites de captura e memória do Switch não
foram ampliados. Desempenho no PC não prevê o desempenho no Tegra/NVK.
