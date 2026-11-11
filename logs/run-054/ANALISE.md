# Capturas 054 — 15/10/2026

## Dados recebidos e preservados

79 pares bin/json, dos frames629 (menu) e1919 (jogo), mais log054. Todos os arquivos bin
têm exatamente o tamanho declarado no JSON. Decoder executado; previews e estatísticas
ficam em capture/. Montagens de contato consultadas para ambos os quadros.
O limite de64MiB truncou a sequência durante o frame; ausência de um passe posterior
não significa falha do jogo nem upload incompleto do usuário.

## Evidências

1. Sequência de blocos de1280x256,1280x256,1280x208. Exemplo:
   1D128000→1D268000→1D3A8000, incrementos0x140000, equivalentes a256linhas de1280x4bytes.
   A reconstrução ilustrativa junta as faixas em720linhas. Não é um frame PC de referência.
2. O shader do personagem (exemplo PS661AA86C99CEBF6C, draw58/frame629) lê t14 em
   1B130000, formato24,2048x3072, mas o cache registra um resolve1024x1024 VK70.
   No1919 existe outro resolve em1B530000: diferença0x400000, compatível com1024linhas
   de2048x2bytes. É um caso concreto de subimagens em endereços diferentes de uma imagem maior.
3. Código anterior indexa resolves pelo endereço exato. CopyToSized só copia o canto da
   primeira imagem para uma view maior; não reúne as outras imagens por endereço.
   Além disso, a view recém-criada espera um resolve futuro para receber conteúdo.
4. No primeiro passe de cena inteiro capturado (629/r18, origem VK97), o personagem já
   tem áreas escuras e a região emissiva estourada. Logo, bloom/apresentação final não são
   explicação suficiente. Passes vermelhos/amarelos intermediários podem armazenar dados,
   não cores finais; não chamar essas cores isoladas de defeito.
5. Conversão de sombra629/r01:347699pixels positivos na origem,347699brancos no destino.
   Comparação de todos os pixels R16 do destino com clamp(SNORM_R*32768)*65535:
   correspondência100%, erro máximo0. O par1919/r01 também corresponde100% à fórmula,
   mas não tem pixels positivos na origem. Isso confirma execução matemática do caminho,
   não sua fidelidade ao Xbox360 nem o motivo de o shader gerar esses valores.

## Correção candidata

TextureCache passa a registrar retângulo realmente escrito e pitch do destino.
Views maiores de resolves R16_UNORM e RGBA8_UNORM tiled podem ser montadas na dimensão
da textura solicitada, reunindo fragmentos compatíveis por endereço.
Suporte limitado a inícios de linhas de macrotiles: exige delta divisível pelo stride,
y múltiplo de32 e confirmação por GetTiledOffset2D do SDK. Não interpreta offsets
horizontais arbitrários, cubemaps,3D, formatos comprimidos, nem emula todo EDRAM.
Fragmentos com pitch declarado incompatível são ignorados. Essa limitação deve ser
vista no resultado e no contador de fragmentos, não ocultada como implementação geral.

Montagem é gravada no command buffer principal após os produtores, encerrando o render pass;
não no setup buffer, que roda antes dos draws. Clear explícito elimina regiões indefinidas.
Barriers cobrem cópias/leitura e conclusão antes dos shaders. Recursos continuam no ciclo
de retire/fences existente. Versão dos resolves invalida caches de respostas/views.
Heap2000,arena448 e ring160 permanecem iguais.

Log[gl-atlas] informa base, tamanho do fetch,pitch,formato,fragmentos e frame.
resolve_atlas=0 em tuning.ini desativa o caminho; padrão1.
NRO anterior graphics_capture preservado com ELF.

Captura por L+R+− permanece: novo orçamento reserva20dos64MiB para ler atlas montados
posteriormente. Marca de endereço0x40000000 indica imagem montada;0x80000000 continua
indicando origem de resolve. Dumps são locais; não publicar recursos do jogo.

Build/hardware e hash do artefato serão registrados no PORT_STATUS após a compilação.
Não afirmar que corrige todos os defeitos ou que a hipótese visual foi validada no console.

## Sugestões externas conferidas no código

- Vulkan no PC: útil para reduzir o espaço de investigação, sem provar sozinho a causa.
  Pacote Windows atual declara REXGLUE_USE_VULKAN=OFF,REX_HAS_D3D12=1; requer build separado
  do SDK. Não basta mudar uma flag do executável atual. PC funcional foi preservado.
- Renderer nativo no PC: base já contém guards e seleção fora do Switch, mas integração
  atual usa heap/telemetria/API específicos do Horizon. Esforço moderado de integração;
  não é necessário reescrever todo o renderer, nem garantido compilar sem alterações.
- Hash de shader: já está no trace e permitiu localizar t14 acima. Faltam comparação
  equivalente com PC e dumps seletivos completos das constantes/descritores.
- Fallbacks: DXN e CTX1 já têm caminhos no cache; isso não prova todos os sinais/gamma
  corretos. Histograma detalhado por formato ainda seria uma instrumentação nova.
- Modos albedo/normal: exigem identificar a função dos recursos/shaders; não são um
  override genérico seguro para qualquer pixel shader recompilado.
- Clocks reais já registrados:052CPU1020,GPU460,8,RAM1331,2MHz. Não inferir overclock necessário.
- Profiler por amostragem já existe (1ms), desativado normalmente. Flag perfil_pilas.flag
  no diretório RexSwitchLogDir o habilita; pausa threads e altera o custo do ensaio.
- Resolução versus FPS é útil se realmente reduzir o trabalho dos passes internos;
  alterar só a resolução da apresentação não isola fill rate. Resultado também pode
  misturar compilação, streaming, CPU e GPU, portanto não é uma prova binária absoluta.

Consulta de referência de packing UNORM, sem copiar código nesta correção:
https://raw.githubusercontent.com/xenia-project/xenia/master/src/xenia/gpu/shaders/pixel_formats.xesli

## PC Vulkan e encaminhamento

O build isolado Windows com Xenos/Vulkan foi concluído e aberto na RTX 3060.
O usuário relatou defeitos semelhantes aos do Switch. O PC funcional D3D12 foi
preservado. A comparação ainda não isola a API: o executável antigo usa outro SDK
e outro conjunto de fontes geradas. Não concluir que NVK seja a causa exclusiva,
nem que todos os erros dos dois renderers sejam iguais.

A amostra preservada em pc-vulkan-baseline.log contém 1978 avisos de fetch de
textura inválido, com cinco descritores distintos (pc-invalid-fetch-summary.json).
No renderer nativo, GetView também devolve uma textura vazia para tipos diferentes
de kTexture, sem que isso antes entrasse no contador unsupported.

Dois launchers locais permitem comparar a opção gpu_allow_invalid_fetch_constants
no PC. A variante permissiva só é um experimento: nenhum bypass foi ativado no
Switch. O usuário aceitou comparar e ainda precisa relatar o resultado visual.
Os launchers usam o mesmo diretório isolado de saves/configuração; não executar
ambos simultaneamente. Os argumentos de census/perf foram acrescentados nos dois,
portanto uma comparação controlada usa o par de launchers, não apenas a sessão
antiga iniciada antes deles.

O próximo NRO mantém a montagem de atlas e acrescenta [gl-fetch]: contagens
cumulativas das chamadas GetView rejeitadas por tipo e até 16 descritores únicos
por cache. Esses números não equivalem a draws, texturas únicas ou pixels ausentes,
pois existe memoização das bindings. Não interpretar um descritor vazio como
prova de material obrigatório ausente.

## Hipóteses para a próxima execução

- Se o atlas reunir mais de um fragmento e a iluminação melhorar, há evidência
  favorável à montagem incompleta. Confirmar a mesma cena, não apenas o FPS.
- Se aparecer só um fragmento ou a imagem continuar igual, verificar os endereços,
  pitches e formatos capturados antes de ampliar regras de associação.
- Se o PC melhorar com descritores permissivos, avaliar esse caso no renderer
  nativo com validação de tamanho/endereço e opção reversível. Não aceitar tipos
  de vertex como se fossem texturas.
- Se o PC não melhorar, comparar com D3D12 no mesmo SDK/código gerado e seguir
  pelos hashes de shaders. Não aumentar heap como tentativa de corrigir cor.

A amostragem de pilhas foi ligada no candidato (profile_sampling=1, intervalo
de 1 ms). Ela pausa threads e pode afetar o FPS; o resultado é diagnóstico,
não uma medição definitiva do desempenho normal.
