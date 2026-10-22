# Perfil de desempenho 069 — 17/10/2026

## Medições no Switch

O perfil por passe muda bastante conforme a cena. Nos blocos mais pesados,
registrou total GPU amostrado de 92,7–112,1 ms por quadro. Os maiores itens
foram:

| Trabalho | Custo observado |
| --- | ---: |
| Passe 1280×720 com cor e depth (`c=t1328p1280f d=t0p1280f`) | 31,85–34,92 ms |
| Resolve de depth 1280×720 | 15,64–21,89 ms |
| Passe seguinte com cor/depth 1280×720 | até 9,47 ms |

O perfil também registrou uma janela de 17,5 FPS e renderer CPU em torno de
52,2 ms/quadro. Em outras cenas, o passe principal e o resolve caem bastante;
os valores acima não são custo constante de todos os quadros. CPU e GPU se
sobrepõem, portanto não se devem somar esses tempos.

O render target informa 1x MSAA, e `nx_msaa` começa desligado. Não há indício
de que o consumo alto venha de MSAA ou falta de heap.

## O que o código faz

O caminho comum do depth resolve lê a imagem de profundidade e grava um destino
R32F por compute shader. A imagem empacotada de depth/stencil também pode ser
necessária para o efeito que restaura o personagem. Pular o resolve ou usar um
cache só pela combinação de endereço e tamanho pode produzir dados antigos,
halos ou regressão visual. A tentativa anterior de cache não teve hits no PC e
não produziu comparação determinística; não foi promovida.

Os dumps FSI do frame 314 mostram repetidamente a combinação de shaders
VS `4E307DC0DBD8F8D2` + PS `54C7484F0F73F4DB` (14 combinações deduplicadas no
conjunto fornecido). O pixel shader contém cinco fetches de textura, operações
de normalização e três `sqrt`. Isso é uma hipótese para investigação, não prova
que esse shader gera o custo de 31–35 ms do passe do Switch: o dump é PC/FSI e
não está sincronizado ao frame do perfil de hardware.

## Situação visual e próxima ação

O usuário relatou que os defeitos gráficos voltaram em uma execução posterior.
Ainda não há correlação dessa regressão com um knob específico. O próximo passo
é relacionar draws e shaders ao passe pesado no Switch, revisar o custo de
índices e gravação de comandos, e comparar qualquer candidato contra uma
captura determinística no PC antes de preparar outro NRO. O NRO de rollback
gráfico fica preservado localmente; executáveis e dumps não são publicados.
