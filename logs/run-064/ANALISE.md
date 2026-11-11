# Execução 064 — captura separada e avaliação da correção de stencil

## Identificação

Não precisa reenviar. A pasta recebida mistura os quadros 936 (063) e 1883
(064), identificados nos nomes. Foram copiados apenas os arquivos f1883 para
`capture/native`: 28 imagens e 121 snapshots de draws. A referência FSI062 foi
reutilizada. O log continua depois da gravação; não houve crash relatado.
Houve novamente truncamento pelo teto de 44 MiB dos dumps não prioritários.

## A variante certa executou

O boot informa `enabled=true applied=true`. Há transferências
`restored R8 -> stencil`, e resolves em `1F6F8000` com
`float24=true stencil=true`. Portanto a ausência de melhora visual percebida
não pode ser explicada simplesmente por abrir o NRO antigo ou deixar o knob
desligado. Não afirmar que o problema gráfico foi resolvido.

## Comparação normalizada

Relatório: `decoded-normalized/comparison.json`. São 26 pares candidatos contra
os dois quadros FSI independentes. Alguns exemplos contra FSI339, erro absoluto
médio em canais normalizados, antes (063) e depois (064):

| Destino | Antes | Depois |
|---|---:|---:|
| 1D128000 | 0,270899 | 0,274100 |
| 1BF30000 | 0,274364 | 0,286946 |
| 1C2C8000 | 0,299682 | 0,124728 |
| 1C660000 | 0,267130 | 0,042972 |
| 1CD90000 | 0,005733 | 0,000023 |
| 1C070000 | 0,286621 | 0,079961 |

Vários destinos ficaram mais próximos; dois iniciais continuam muito diferentes.
Os quadros não sincronizam animação/tempo e médias incluem fundo. A tabela não
mede qualidade final nem prova que toda diferença foi causada pelo patch.
O relato do usuário continua sendo pouca ou nenhuma melhora visual perceptível.

## Próxima pista apoiada pelos bytes, sem mais hardware

O shadow target anterior é RG16_SNORM, tile-base 1216, pitch 1040, dimensão
1040×1024. Depois o jogo usa a mesma EDRAM em targets RGBA8 de bases 1280,1536
e1792 com pitch1280. O nativo separa essas imagens por chave e não propaga os
bits antigos entre elas. As máscaras parciais de escrita preservam componentes
que deveriam conter os dados do produtor anterior.

Foi feita uma reconstrução **no PC** dos bytes do shadow target nativo, usando
tiles de cor32bpp 80×16: para um pixel do target-base1280, calcula-se o tile
físico e sua coordenada correspondente no produtor-base1216/pitch1040. Aplicado
também o swap R/B do resolve. Não é uma cópia linear nem resize de imagem.

Resultado em `shadow-alias-check.json`: previsão cobre os 327680 pixels do
recorte1280×256; G/A coincidem simultaneamente com FSI339 em94,79% dos pixels.
O fundo é frequente, então esse percentual não é prova estatística isolada.
Os exemplos por endereço e os estados de escrita oferecem uma pista concreta:

- Pixel(500,100) mapeia ao produtor(820,196). Os bytes previstos reinterpretados
  em RGBA são `[0.0039,0.502,0.0039,0.502]`, exatamente os do FSI. O nativo
  não herdava esse conteúdo entre imagens.
- Pixel(900,150) mapeia a(900,262), mesmo conteúdo anterior. O FSI preserva os
  canais mas altera o canal da máscara para1 durante o draw seguinte, consistente
  com a escrita parcial. A reconstrução do produtor anterior não deveria tentar
  reproduzir essa escrita nova por conta própria.

Assim, a correção de stencil era uma lacuna real, mas não cobre a reutilização
de memória entre targets de **cor** com formatos/pitches diferentes. Próxima
implementação deve acompanhar produtores/ownership e transferir conteúdo
preservado antes das escritas parciais. Evitar preencher G/A com constantes
observadas nesta cena: elas são dados da EDRAM, não cores fixas do jogo.

Não foi criado outro NRO nesta análise. Os arquivos recebidos bastam para
continuar; não pedir repetição da captura 064 nem do FSI por causa da mistura.
