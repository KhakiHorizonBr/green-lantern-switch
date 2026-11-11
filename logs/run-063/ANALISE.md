# Execução 063 — captura utilizável e comparação inicial

## Resultado da captura

Usuário observou que não houve crash. O log confirma que o quadro 936 foi
capturado e que o jogo continuou rodando até 11:28:27. A pasta contém 28 imagens
de passes e 121 snapshots de entradas dos draws, com seus JSONs e registradores.
Houve truncamento pelo orçamento de 44 MiB dos dumps não prioritários: não é uma
captura de todos os destinos do quadro. Não confundir com falha de alocação do
jogo. Não foi necessário repetir a referência FSI da sessão 062.

O caso de crash da captura 061 não se repetiu após a gravação por cópia em
páginas isoladas. Isso valida a mudança para esta sessão; não prova ausência
de outros crashes.

## Comparação feita no PC

Dados preservados em `capture/native` e `capture/fsi`. O script
`switch/tools/compare-renderer-captures.py` reconstruiu tiled 2D do FSI e recortou
os retângulos copiados das imagens lineares nativas. Detectou 26 pares candidatos
(13 destinos nativos versus dois quadros FSI independentes). Animação e estado
temporal não são sincronizados; diferenças não são automaticamente bugs.

Foi corrigida uma omissão do decodificador: no nativo, `copy_dest_swap` é aplicado
pela view de sampling; no FSI ele já está nos bytes escritos. O comparador agora
normaliza essa diferença conhecida para RGBA8 lendo RB_COPY_DEST_INFO do snapshot.
Isso altera a análise no PC, não o renderer nem um NRO.

Foram decodificados 110 destinos ao todo. Quatro destinos depth/array e dois
de formato Xenos 7 foram explicitamente omitidos. Source images e atlases
permanecem preservados, mas não são tratados como destinos equivalentes.

## Achados

- Resolve R16 `1B130000`, recorte 1024×1024, pitch 2048: 99,04% dos pixels
  coincidem exatamente com FSI quadro 339. Erro absoluto médio normalizado
  0,00957. Contra FSI314 a coincidência é 66,82%, mostrando que a escolha do
  quadro/animação importa. Isso valida apenas esse recorte, não todo o atlas.
- O primeiro resolve RGBA8 `1D742000` difere muito fora dos pequenos elementos
  desenhados. FSI exibe conteúdo antigo/indefinido nessa área, nativo zeros.
  Não usar a média global desse passe como prova da primeira falha gráfica.
- Outros destinos do personagem divergem, inclusive após normalizar o swap.
  Por exemplo, em `1D128000`, pixel (900,150), nativo RGBA em armazenamento
  aproximadamente `[1.0, 0.0039, 0.0, 0.0]` e FSI
  `[0.0039, 0.502, 1.0, 0.502]`. O swap explica
  a troca R/B, mas não o restante das diferenças. Os números isolados não
  identificam o produtor errado nem substituem comparação de cobertura.
- Draws nativos com PS F57B6BC95EB4EC9B e 09B540165140DBF6 encontram candidatos
  FSI de mesmo VS/PS e zero diferenças no bloco de render state. Fetch e
  constantes float ainda diferem, inclusive endereços próprios de cada sessão.
  Nem todos os draws possuem parceiro: FSI não registrou draws sem pixel shader.

## Candidato concreto para a investigação seguinte

O draw nativo d18 usa simultaneamente:

- Cor: tile 0, formato RGBA8, pitch 1280, máscara de escrita R apenas.
- Profundidade: tile 0, formato D24FS8, pitch 1280.
- PS F57B6BC95EB4EC9B; duas leituras do destino de profundidade `1F6F8000`,
  uma como profundidade e outra como bits de textura.

No Xenos/FSI os dois usos compartilham EDRAM. No renderer nativo os objetos de
cor e profundidade têm imagens Vulkan distintas e chaves separadas pelo campo
`is_depth`; uma escrita na cor não atualiza automaticamente a profundidade.
Esse é um caso real de aliasing a auditar, especialmente o stencil e os passes
posteriores que dependem dele. Não assumir uma cópia pixel a pixel trivial:
o endereçamento dos tiles de cor/profundidade também precisa ser respeitado.

O trace contém outros usos de regiões que se sobrepõem. Isso torna inadequado
atribuir todos os defeitos a uma única textura ou corrigir apenas valores de
clear sem examinar a sequência de produtores e consumidores.

Esse candidato não é ainda uma causa comprovada de todos os defeitos. Antes
de criar outro NRO, seguir a escrita compartilhada e o consumidor no código,
usando os dumps existentes. Não aumentar a memória com base nesta comparação.

## Artefatos locais

- `summary.json`: contagens e formatos omitidos.
- `decoded-normalized/comparison.json`: pares de pixels e entradas candidatas.
- `decoded-normalized/*.png`: previews, não imagens finais do jogo.
- `green_lantern_063.log`: log recebido, preservado.

Nenhum dado derivado do jogo foi publicado. Não foi criado outro NRO nesta
etapa; o avanço foi corrigir a captura, coletar evidência e estreitar a causa.
