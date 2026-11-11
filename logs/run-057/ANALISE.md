# Execução 057 — Xenos/Vulkan FSI no Switch

## Confirmado

- Mesmo ELF preservado da variante inicial (v1), base runtime 0x39e9bf8000.
- Log: requested='fsi' selected=fsi fsi_supported=true; todas as opções obrigatórias aplicadas.
- Usuário viu menu sem os bugs visuais anteriores, com FPS muito baixo e crash posterior.
- Perfil tem intervalos de 18,6/18,8 FPS e intervalos finais de 0,2–3,9 FPS. Não usar
  a taxa de uma cena como desempenho de todo o teste. Amostragem de stacks desligada.
- CPU portátil a 1020 MHz, GPU 460,8 MHz e RAM 1331,2 MHz.
- Emulação de leituras continua elevada: 119.299/s em um intervalo inicial;
  39.422–98.668/s nos últimos intervalos mostrados. É um custo real de CPU, mas a
  proporção física versus tabelas CPU e o tempo da GPU não foram medidos aqui.

## Crash atual, separado do histórico anexado

rex_crash.log contém registros antigos anexados. O ÚLTIMO corresponde à base
da sessão 057 e ao erro final do log principal: leitura guest 0x6F6656D0.
PC ELF+0xA0E774 = sub_8233E7D8; instrução ldr w0,[x20,w2,uxtw]. Disassembly
corresponde ao incremento de contador do guest em 0x8233E958 (bloco 8233E950).
O endereço é resultado de ponteiro/índice de uma estrutura do jogo; a origem da
inconsistência não foi identificada. Não mascarar o acesso nem retornar zero.
Não atribuir os bad_alloc de registros históricos ao crash atual.

Pilha simbolizada: sub_8233E7D8 -> sub_8233FE50 -> sub_823132B0 -> sub_823102E8
-> sub_82310D30 -> sub_82311238 -> sub_824277A8 -> sub_8237AF60 -> sub_8237B778
-> sub_824EAFD8 -> sub_824EB5B0 -> sub_824E89E0 -> xstart.

## Variante v2: reaproveitamento limitado da otimização de leituras

Adicionar RexGmSetCpuReadOnlyMapped, desligado por padrão. Na variante FSI v2,
mapear como RW apenas páginas logicamente READ em backing virtual não físico.
Manter proteção lógica READ. Selecionar pelo offset REAL da view, abaixo de
0x100000000, e recusar intervalos que cruzem views/limite físico. Isso exclui
também a view virtual 0x7F..., que é alias físico, além de A/C/E/raw.
Proteções NONE continuam intocadas. Isso relaxa a proteção contra escritas
indevidas em tabelas somente de leitura da CPU, como a otimização do renderer
nativo, mas preserva as watches físicas usadas por Xenos/FSI.

Não equivale a portar a coerência inteira do renderer nativo. Não ligar a opção
global readonly_mapped no Xenos: ela mapearia também páginas físicas vigiadas.
Não há correção demonstrada do crash da estrutura do guest nesta alteração.

Artefato: switch/artifacts/green_lantern_xenos_fsi_cpu_reads.nro (53.962.949 bytes).
SHA256: 64FC5AD71FD66A9BD50CF1E0C179B3036CA491769BFF0DE8F2EBCA13CB78D6E5.
ELF correspondente preservado. Build Release concluído; hardware pendente.
FSI, heap, arena, shaders, resolução, saves e cache permanecem com a configuração
da variante v1; sampling invasivo continua desligado. NRO v1 preservado.

### Interpretação do próximo teste

- Queda das leituras emuladas com imagem correta: a parcela CPU-only foi relevante.
- Leituras ainda altas: watches físicas são dominantes; será preciso adaptar
  rastreamento de conteúdo/coerência, sem desligar avisos de escrita indiscriminadamente.
- Leituras caem mas FPS continua baixo: medir custo de GPU/FSI e outros trabalhos CPU.
- Mesmo crash: investigar a estrutura do guest; a otimização não pretendia consertá-la.
- Novo defeito visual: comparar logs e voltar ao v1; não presumir ganho sem correção.
