# Publicação — 17/10/2026

Fontes do renderer nativo e integração Switch sincronizados com a árvore de
trabalho local. Documentação atualizada com o perfil 069, os limites da análise
de shader e a regressão visual relatada pelo usuário.

O perfil aponta custo variável por cena no passe 1280×720 e no depth resolve;
MSAA aparece em 1x e não há pressão de heap demonstrada. O shader repetido nos
dumps FSI permanece uma hipótese, não uma causa confirmada do tempo no Switch.

Não há novo NRO validado no Switch nesta atualização. Executáveis, arquivos de
jogo, saves, dumps binários, imagens de readback e capturas completas continuam
locais. O rollback gráfico conhecido também permanece local.

Consulte [estado atual](docs/PORT_STATUS.md), [resultados 069](docs/RESULTADOS_069_FPS.md)
e [histórico 054–059](docs/RESULTADOS_054_059.md).
