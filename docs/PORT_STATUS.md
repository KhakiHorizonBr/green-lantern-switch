# Estado atual — 24/10/2026

## Resumo

O renderer nativo chega ao gameplay. O usuário confirmou a correção dos defeitos
gráficos principais no068; estabilidade e gráficos de todas as fases ainda
não foram validados. A prioridade atual é desempenho.

No074, menu até24,8FPS; fase pesada7,3–8,8FPS. Não há30FPS sustentados.
O alvo35–40FPS foi pedido pelo usuário, mas ainda não foi atingido nem previsto.
O pacing atual nx_min_frame_vblanks=2 também limita o caminho nativo a30FPS.

## Fatos confirmados

- Hardware074 usa uploads esparsos sem sombra quando o cache está cheio;
  fallback por orçamento0. Isso poupou cópias, mas não trouxe ganho global
  sustentado comprovado na fase pesada.
- CPU renderer ~110–131ms; GPU ~70–80ms em janelas pesadas. Tempos se sobrepõem.
- Há esperas de ~59–69ms por espaço no stream em alguns quadros. A seção de
  vértices abrange varredura, alocação e esperas, não apenas memcpy.
- Heap2000MiB, backing arena384MiB, stream160MiB; sombras até32MiB.
- Leitura emulada falha0 nos perfis recentes; sem pressão automática de memória.
- Clocks portáteis observados1020/460,8/1331,2MHz (CPU/GPU/RAM).
- Conversão direta de depth/stencil e EDRAM sob demanda têm comparações de bytes
  PC com zero diferenças nas amostras. Não representam validação do jogo inteiro.
- Upload esparso final:6buffers,7.889.024bytes, zero diferenças contra a memória
  original;2buffers sem sombra. Replay150s chegou à fase e encerrou com saída0.

## Controles e próximo teste

Usuário escolheu posições Xbox: Switch B→A, A→B, Y→X, X→Y.
A integração compilada força esse padrão, com input_xbox_layout=1 no tuning.ini
(0 escolhe letras iguais). O boot registra gl-input. Usuário preferiu adiar
novo teste de hardware até haver otimização adicional. Controles não estão
marcados como validados no console.

## Próximo trabalho

Separar CPU ativa e espera dentro do upload/Allocate; reduzir o trabalho GPU
que ainda excede33,3ms na fase; comparar primeiro no PC. Avaliar mudanças de
pacing acima de30FPS somente com cuidado com a velocidade da lógica do guest.
Nenhum novo candidato de desempenho posterior ao074 foi validado no Switch.

## Limites e tentativas a não promover sem evidência

- Mais RAM sozinha não foi demonstrada como solução para o gargalo atual.
- Cache de resolve de profundidade com chave parcial permanece desligado:
  stencil pode mudar independentemente de Z.
- A tentativa de copiar trechos inalterados de buffers GPU antigos não mostrou
  ganho PC suficiente e foi descartada.
- Contadores antigos de300quadros não provavam que a otimização parava na fase;
  agora gl-vertex-window acompanha janelas de10s.
- Não remover sincronização do presenter por interpretar toda espera como custo inútil.

## Detalhes e histórico

- [Resultados071–074](RESULTADOS_071_074.md)
- [Depth direto](FPS_DEPTH_CACHE.md), [EDRAM sob demanda](FPS_LAZY_EDRAM.md)
- [Uploads e tentativas](FPS_VERTEX_DELTA.md), [cache e upload sem sombra](FPS_VERTEX_CACHE.md)
- [Diário completo preservado](PORT_LOG_2026-10-24.md)
- [Logs074](../logs/run-074/)
