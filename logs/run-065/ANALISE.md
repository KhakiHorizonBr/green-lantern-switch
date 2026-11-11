# Execução 065

Captura f827: 28 imagens, 121 snapshots de draws. Arquivos copiados da pasta
native para isolar esta rodada. Comparação binária com f1883 da execução064:
4 imagens iguais e24 diferentes. Cenas animadas independentes: diferença de hash
não mede melhora visual. O relato do usuário continua sendo mesmos defeitos.

Cache ativo, zero importações nos contadores f300/f600/f900,2048tiles desconhecidas.
Publicações acumuladas f900:2982480. O mecanismo descartava os dados antes do
uso; esse resultado não refuta a hipótese de aliasing entre alvos de cor.

Os snapshots de clear mostram scissor8192x8192 e modosMSAA4x, com targets host1x
já expandidos. O código anterior multiplicava pitch/retângulo novamente e
invalidava desde a base até o fundo, ou tudo ao cruzar2048tiles. Mesmo o clear
uniforme de corMSAA era classificado como escritor não suportado, impedindo a
recuperação do produtor. V2 corrige essas regras; resultado GPU ainda pendente.
