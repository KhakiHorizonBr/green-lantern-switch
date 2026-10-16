# Referências estudadas e limites de reaproveitamento

## Rayman Origins

[Projeto consultado](https://github.com/XDanfr/RaymanOriginsRecomp), revisão
`c1bf7b90e95d476e63aa4a0b077905ff6dbde9bd` em 16/10/2026.
O código/documentação consultados mostram renderer nativo por interceptação de
chamadas D3D, shaders antecipados, cache Vulkan persistente e preparação dos pipelines
conhecidos no início. Também documentam dois frames em voo e uploads menores.
As medições descritas são de Android/macOS; a descrição do fork mencionar Switch
não demonstra que os números de FPS se aplicam ao Switch.

Cache e preparação antecipada podem ajudar nossas pausas, mas não resolvem sozinhos
o intervalo aquecido de 1,4 FPS sem novas compilações. Não foi copiado código desta
referência nesta atualização; o repositório consultado traz licença GPLv3.

## XenosRecomp

[Projeto](https://github.com/hedge-dev/XenosRecomp): converte shaders 360 para HLSL,
compilável em DXIL/SPIR-V. Não é um renderer completo nem implementa sozinho EDRAM.
O próprio README documenta funcionalidades incompletas e adaptações específicas
para Sonic Unleashed. Rayman corrige casos próprios de instruções e constantes.
Possível referência para um caminho nativo/híbrido, mediante validação por shader;
não é substituição direta do backend FSI e não elimina a compilação para a GPU alvo.

## Outro Green Lantern para PC

[MrPowerUp82/green_lantern_recomp](https://github.com/MrPowerUp82/green_lantern_recomp),
revisão consultada `6289f89054651839899744e6bfc6e02e99ab9a18`.
O estágio publicado conta draws e apresenta uma cor de fundo; a validação registra
imports incompletos e bloqueios da recompilação do executável completo. Pode servir
de referência para testes isolados de comandos, mas não oferece correção pronta
dos materiais pretos ou renderer completo. Nenhum código incorporado.

## Referências anteriores

O renderer nativo continua derivado de The Simpsons Game NX, com consultas a
Project8 Switch, nfsmw-nx e KisakCOD, conforme avisos e documentação já publicados.
Não foram importados recursos ou shaders dos outros jogos. A variante FSI utiliza
o backend Xenos/Vulkan do ReXGlue/Xenia com adaptações de plataforma deste projeto.
