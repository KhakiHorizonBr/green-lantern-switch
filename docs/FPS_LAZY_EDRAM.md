# Transferências EDRAM sob demanda — 22/10/2026

## Motivo

No log071 a conversão direta de depth/stencil já estava ativa. CPU continuava
em173–196ms em cenas pesadas, com seção índices45–56ms e resolves62–65ms.
A seção índices inclui BeginRecording, caches e alocações, não só conversão.
Foram adicionados tempos separados: início de gravação, decode, hash/lookup,
alocação e conversão. Não prometer ganho sem medir o console.

## Mudança funcional

Antes, FlushEdramWrites exportava todos os tiles suportados ao terminar cada
passe, mesmo se nunca fossem importados em outro alvo. Agora registra a versão,
o produtor e `needs_export`; ImportEdramColor exporta apenas os tiles realmente
lidos por outro alvo e os importa após a barreira de transferência.

- Origem continua sendo a imagem do produtor, com transferências bit a bit.
- Novas escritas substituem a versão e o produtor dos tiles tocados.
- Escritas desconhecidas/de profundidade continuam invalidando o estado.
- Limpezas uniformes zero continuam publicadas imediatamente.
- Os alvos ativos não são descartados por idade; crescimento preserva o conteúdo
  antigo e a busca usa a chave estável do produtor, não ponteiros aposentados.
- Produtores ausentes ou regiões inválidas não importam bytes antigos do buffer.
- Resolução, shaders, profundidade direta, heap e arena não mudam.

Opção: `nx_edram_lazy_export` no PC; `edram_lazy_export=0` no tuning.ini do
Switch restaura as exportações antecipadas, sem recompilar.

## Validação

Sonda só PC mantém as exportações antecipadas como referência.
Na mesma importação, captura o resultado antecipado e depois o resultado obtido
exportando do produtor sob demanda. A comparação dupla não existe no Switch.
Script e relatório em `outputs/fps-lazy-edram`.

Frame2000 do caso `edram-lazy-verify`: duas importações, 2.621.440bytes,
zero diferenças. Ambas têm mais de1.204.000bytes não zero. Caso encerrou após
100s pela automação, saída0. Isso valida os imports capturados nessa cena,
não todas as combinações de alvo que possam aparecer no jogo.

Builds PC e Switch concluídos. Caso `edram-lazy-performance` sem sonda dupla:
100s, chegou à fase, saída0 ao encerramento pela automação. Nos últimos trechos
estáveis, CPU5,5–6,2ms e GPU7,3–7,7ms, contra CPU6,5–7,4ms e GPU9,0–9,3ms
na rodada anterior. CPU de resolves0,86–0,87ms, antes1,78–1,83ms. Replays
distintos da mesma cena; isso não é previsão de FPS nem fator de aceleração ARM.
Captura final inspecionada com cenário e personagem visíveis.

Ainda não há medição do novo NRO no Switch. Candidato:
`outputs/fps-lazy-edram/green_lantern_fps_edram_sob_demanda.nro`.

## Atualização hardware072

Usuário confirmou melhora e menor atraso no menu. O perfil mediu21–26FPS nos
trechos leves/menu e aproximadamente8–13FPS na fase pesada. CPU de resolves
caiu para4,9–7,8ms nas janelas de fase, contra62–65ms no071. Exportações sob
demanda ativas. Mantidas a alteração e as evidências do controle anterior.
Próximo foco: preparação/cópia de vértices e apresentação; ainda não há30FPS
sustentados. Logs em `outputs/fps-lazy-edram/hardware-072`.

Rollback071 preservado em `outputs/fps-lazy-edram/rollback-071.nro`.
