# Escala interna seletiva — 05/11/2026

**Resultado posterior, hardware 092:** aplicação de 75% confirmada, sem ganho
demonstrado e com trechos de regressão. A variante de conversão por scope não
foi promovida; usar 100%. Próximo foco: coerência entre representações e
conversões somente nos consumidores necessários. Relatório local completo em
`outputs/run-092/RESULTADO_092.md`. O 090 era log de deko3d v9 e não conta como
teste desta variante Vulkan.

Entrega experimental isolada em `outputs/internal-scale/`. A ablação do
hardware 089 mostrou redução relevante do custo GPU com menor cobertura.
Foi implementada escala do raster principal com attachments menores, mantendo
as superfícies e endereços guest no tamanho original por conversões nas bordas
dos scopes. Os prepasses que compartilham a profundidade principal também são
reduzidos; reduzir só os materiais gerou artefatos no PC e foi rejeitado.

Menu e fase foram inspecionados no PC em 100%/75%, com flags equivalentes aos do
Switch (`GuestDepth=true`, `VertexSafety=8`), mesmo EXE/DLLs, dois replays de 90 s.
Ambas as execuções encerraram com código zero. Em 75%, 49 variantes de PS foram
criadas, 17 com coordenadas FragCoord adaptadas. Imagem inteira e personagem
iluminado nas capturas. Não há resultado de hardware desta variante ainda.

O PC não demonstrou ganho: última amostra GPU total 7,4 ms em 100% e 9,5 ms em
75%, com cenas próximas, não determinísticas. Conversões extras custam banda e
podem anular a economia no Tegra. Não promover nem prometer FPS antes de medir
o total no Switch. A heap e os clocks não foram aumentados. As superfícies
temporárias aumentam o consumo de memória; 100% não as usa.

NRO separado: `outputs/internal-scale/green_lantern_escala_interna.nro`.
SHA-256: `07c73357cdc956d85312d5a2ab00a7ba5df1c427e6b16d8e7b881729dbde2fc0`.
Padrão de escala 100%. Experimento solicitado no tuning.ini:

```ini
internal_scale_percent=75
ablation_mode=0
partial_submit_draws=0
```

100 desliga a escala no mesmo NRO. O NRO do diagnóstico 088/089 permanece em
`outputs/perf-ablation/` como rollback. Fontes compartilhados com deko3d não
foram alterados. Detalhes, ressalvas, testes rejeitados e evidências estão nos
arquivos `LEIA-ME.md`, `VALIDACAO.md`, `pc-validation.json`, `manifest.json` e
`paired-aligned.png` da pasta de entrega. Esta rodada ainda não foi publicada
no GitHub.
