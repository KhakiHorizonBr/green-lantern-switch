# Escala interna — verificação de 05/11/2026

Atualização após hardware 092: a escala foi aplicada e não demonstrou ganho;
esta versão com conversão em cada scope foi rejeitada como otimização normal.
Voltar a 100%. Evidências e próximos passos em `outputs/run-092/RESULTADO_092.md`.

## Estado

Protótipo com imagem completa aprovado na inspeção das capturas de menu e
gameplay do PC. Nenhum resultado no Switch ainda. A opção permanece desligada
por padrão e não foi promovida como otimização comprovada.

## Verificações concluídas

- `check-fragcoord.cmd`: transformação de shader sintético com cargas vetoriais
  e escalares de FragCoord; rejeição de dados truncados/vazios; asserts ativos.
- `internal-scale-75-aligned` e `internal-scale-100-aligned`: mesmo executável e
  DLLs, replay igual, 90 segundos cada. `GuestDepth=true` e `VertexSafety=8`
  explícitos nas duas sessões. Código de saída zero nas duas execuções.
- 49 variantes de pixel shader criadas no caso 75%; 17 possuem cargas de
  FragCoord corrigidas. Sem avisos de formato sem blit, transformação de shader
  não suportada, `VK_ERROR_DEVICE_LOST`, `vkQueueSubmit failed` ou linhas critical
  nesses logs. Isso não substitui validação completa por validation layer.
- Superfícies menores e contadores de scopes/blits confirmam aplicação real da
  escala. O modo 100% não criou variantes nem superfícies menores.
- Capturas em `paired-aligned.png`: quadro completo, personagem iluminado no
  menu e imagem da fase nas duas variantes. A menor definição é esperada.
  Os replays por tempo não fornecem quadros determinísticos; não exigir igualdade
  byte a byte entre imagens em resoluções diferentes.

## Tentativas rejeitadas e custos

As primeiras sessões smoke usaram flags antigos do launcher, produzindo o
defeito antigo do personagem escuro. Foram excluídas da referência visual.
Depois, reduzir só o material e deixar prepasses em resolução nativa causou
linhas no personagem mesmo com flags corretos (`75-correct`). Essa tentativa
foi rejeitada. A versão final inclui todos os scopes que compartilham a
profundidade principal e passou pela nova inspeção.

No último perfil por passe, o PC registrou 7,4 ms totais em 100% e 9,5 ms em
75%, com cerca de 460/456 draws no passe principal. São cenas próximas, não o
mesmo frame. O resultado não demonstra ganho no PC: as conversões custam tempo.
Não extrapolar ganho para o Tegra. A decisão depende do total medido no Switch,
incluindo conversões, waits e preparação. O limitador de 30 FPS do PC impede
usar a média de FPS para provar ganho quando ambos cabem no orçamento.

Este primeiro caminho usa dez superfícies temporárias no replay observado e
converte attachments ao redor de cada scope. Ele pode aumentar custo de banda
ou memória. Se o total piorar no Switch, retornar a 100%; o objetivo seguinte
será manter dados reduzidos por mais tempo e converter somente nos consumidores
que precisem da representação guest, com rastreio explícito de validade.

## Evidências

`pc-validation.json`: hashes, argumentos, contadores de aplicação e amostras de
GPU. `build-pc-aligned.log`: build PC final. `build-switch.log`: build Switch.
`LEIA-ME.md`: funcionamento, limites e rollback. `manifest.json`: artefatos e
hashes após o empacotamento. Nenhuma atualização GitHub feita nesta rodada.
