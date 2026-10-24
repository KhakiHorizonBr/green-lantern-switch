# Resultados e limites — 24/10/2026

## Estado confirmado

O usuário confirmou a correção dos defeitos gráficos principais do renderer nativo
no hardware068. Buffers de fetch indireto completos, intervalo de profundidade
do guest e restauração de stencil foram preservados nas rodadas de desempenho.
A comparação PC permitiu reproduzir e investigar os defeitos sem pedir cada
experimento no Switch. Não se demonstrou correção de todas as cenas do jogo.

| Execução | Mudança principal | Resultado observado |
| --- | --- | --- |
| 071 | Conversão direta de depth/stencil e cópia rápida de índices | GPU pesada ~88–95 ms; CPU ~173–196 ms; sem 30 FPS |
| 072 | Exportar EDRAM apenas quando outra imagem precisa dos tiles | Menu ~21–26 FPS; fase pesada ~8–13 FPS; resolves CPU ~5–8 ms |
| 073 | Copiar dados e preencher zeros de vértices na GPU | Menu até 24,8 FPS; fase pesada ~8 FPS; sem ganho global sustentado demonstrado |
| 074 | Cache seletivo e upload esparso mesmo sem sombra disponível | Menu até 24,8 FPS; fase pesada 7,3–8,8 FPS; fallback por orçamento zerado, mas esperas/GPU persistem |

Sessões e cenas não são replay idêntico. Não tratar a tabela como benchmark
controlado nem somar tempos CPU e GPU, que podem se sobrepor.

## Evidências PC

- Depth/stencil direto: seis buffers, 22.118.400 bytes, zero diferenças.
- EDRAM sob demanda: duas importações, 2.621.440 bytes, zero diferenças.
- Upload esparso final: seis buffers, 7.889.024 bytes, zero diferenças contra
  a memória original do guest, incluindo dois buffers sem sombra persistente.
- Replay final de 150 segundos chegou à fase e encerrou com saída 0.
- Capturas pontuais e um replay curto não validam o jogo inteiro.

## O que não funcionou como esperado

Copiar trechos inalterados de buffers GPU antigos não trouxe ganho PC suficiente;
a tentativa foi descartada. Menos bytes escritos pela CPU no upload esparso
não garantiram mais FPS no Switch. A seção de vértices inclui Allocate e espera
por espaço no stream; no074 há esperas pontuais de ~59–69 ms. Não atribuir toda
a seção a memcpy ou CPU ativa.

Correção de leitura do073: os contadores antigos saíam a cada 300 quadros. A fase
acabou antes de outro relatório. A ausência de novas linhas não demonstrava
que a otimização tinha parado. gl-vertex-window agora acompanha janelas de 10 s.

O cache de depth só por estado parcial continua desativado: stencil pode mudar
sem mudar a profundidade. Não recuperar esse experimento como otimização pronta.

## Orçamento e controles

Heap total 2000 MiB, backing arena 384 MiB e upload stream 160 MiB. Sombras de
vértices limitadas a 32 MiB, com até 128 KiB de classificação adicional.
Orçamentos não são somáveis cegamente: backing e driver podem estar no malloc.

Usuário escolheu controles por posição Xbox: Switch B→A, A→B, Y→X, X→Y.
A integração aplica input_xbox_layout=1 por padrão e permite 0 no tuning.ini
para letras iguais. Build de controles concluído; usuário preferiu adiar o
novo teste no Switch até haver mais otimização. Não marcar como validado no hardware.

## Próximo objetivo

Usuário pediu 35–40 FPS e menos rodadas no console. O caminho nativo atual tem
nx_min_frame_vblanks=2 (limite de 30 FPS); o desempenho pesado está bem abaixo
desse limite. Primeiro reduzir execução GPU, esperas e tráfego; depois avaliar
outro pacing e a velocidade da lógica do jogo antes de prometer mais de 30 FPS.
Clocks observados são de portátil: CPU 1020 MHz, GPU 460,8 MHz, RAM 1331,2 MHz.
Ainda não há 30 FPS sustentados nem uma previsão confiável para 35–40 FPS.

Fontes atuais, shaders próprios, integração, overlay do SDK e logs textuais estão
incluídos. Assets, XEX, saves, capturas binárias e NROs permanecem locais.
