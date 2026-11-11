from pathlib import Path
import hashlib, json, shutil, struct, zipfile

root = Path(__file__).resolve().parents[2]
project = root / 'work/green-lantern-360/project'
build = project / 'switch/build/generated-full-runtime'
package = root / 'outputs/green-lantern-deko-package'
runtime = package / 'switch/green_lantern'
shaders = runtime / 'deko_shaders'
nro = runtime / 'green_lantern_deko.nro'
shutil.copy2(build / 'green_lantern_switch_deko.nro', nro)
rollback = package / 'rollback/green_lantern_vulkan_074.nro'
rollback.parent.mkdir(parents=True, exist_ok=True)
shutil.copy2(root / 'outputs/controls-075/rollback-074.nro', rollback)

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

data = nro.read_bytes()
assert data[0x10:0x14] == b'NRO0', 'NRO header invalid'
image_size = struct.unpack_from('<I', data, 0x18)[0]
assert 0x80 <= image_size <= len(data)
assert data[image_size:image_size+4] == b'ASET', 'NRO assets missing'
lines = [line.split() for line in (shaders / 'modules.txt').read_text().splitlines() if line.strip()]
assert len(lines) == 122
for fields in lines:
    assert (shaders / fields[3]).is_file(), fields[3]
for name in ('output.vert.glsl.dksh', 'output.frag.glsl.dksh', 'copy_color.frag.glsl.dksh', 'restore_stencil.frag.glsl.dksh', 'depth_resolve.dksh'):
    assert (shaders / name).is_file(), name
dksh = sorted(shaders.glob('*.dksh'))
for path in dksh:
    assert path.read_bytes()[:4] == b'DKSH', f'Invalid DKSH: {path.name}'

readme = '''# Lanterna Verde — NRO deko3d experimental 001

O NRO do jogo está compilado, com o parser PM4 conectado ao deko3d.
Este é o primeiro pacote desse backend: ainda não foi executado no Switch.
Não há resultado de FPS ou equivalência visual medido para este NRO.

## Instalação

1. Copie a pasta `switch` deste pacote para a raiz do cartão SD, mesclando as pastas.
2. Copie também `deko_shaders`: o NRO sozinho não funciona. Ela deve ficar em
   `sdmc:/switch/green_lantern/deko_shaders/`.
3. Os arquivos do seu jogo continuam em `sdmc:/switch/green_lantern/game/`.
4. Abra o homebrew em modo aplicativo, segurando R ao iniciar um jogo, como antes.
5. Selecione **Lanterna Verde - deko3d experimental** (`green_lantern_deko.nro`).

Não é necessário apagar o NRO Vulkan. A pasta `rollback` contém uma cópia
do NRO Vulkan 074; ela fica fora da pasta de instalação automática.

## O que este pacote contém

NRO AArch64 real do jogo, 122 variantes de shaders capturadas, 77 módulos
auxiliares (incluindo conversão de profundidade), manifesto e validação.
O caminho deko3d usa sua própria apresentação e não cria dispositivo Vulkan.
Componentes Vulkan do SDK ainda podem existir no executável ligado.
Heap total: 2000 MiB; arena de backing guest: 384 MiB;
arena de vértices: 160 MiB; uploads: três slots de 32 MiB.

## Limites desta primeira versão

A cobertura AOT depende dos shaders já capturados. Variantes novas são
registradas e ignoradas, podendo faltar desenhos em cenas não cobertas.
Alguns recursos, como texturas 3D e determinadas conversões de EDRAM/MSAA,
permanecem sem suporte. A compilação e os testes CPU não provam que esses
comandos de GPU funcionarão corretamente no console.

Depois do teste, envie o log `green_lantern_XXX.log` mais recente e uma foto
do resultado; se fechar com erro, envie também `rex_crash.log` e `rex_stderr.log`.
Os shaders deste pacote foram derivados da sua cópia local: não publicar
este pacote no repositório público.
'''
(package / 'LEIA-ME.md').write_text(readme, encoding='utf-8')
validation = {
    'date': '2026-10-29', 'target': 'green_lantern_switch_deko',
    'NROGenerated': True, 'PM4GameIntegrated': True,
    'DekoGPUExecuted': False, 'HardwareFPSMeasured': False,
    'nro': {'file': str(nro.relative_to(package)), 'bytes': len(data),
            'sha256': digest(nro), 'magic': 'NRO0', 'image_size': image_size, 'assets_magic': 'ASET'},
    'elf_machine': 'AArch64', 'game_shader_variants': len(lines),
    'dksh_modules': len(dksh),
    'assets': [{'file': str(path.relative_to(package)), 'bytes': path.stat().st_size, 'sha256': digest(path)} for path in [shaders / 'modules.txt', *dksh]],
    'rollback': {'file': str(rollback.relative_to(package)), 'sha256': digest(rollback)},
    'build_log': 'outputs/deko-native/build-nro-final.log',
}
(package / 'validacao.json').write_text(json.dumps(validation, indent=2, ensure_ascii=False), encoding='utf-8')

status = '''## NRO deko3d experimental 001 gerado — 29/10/2026

O target `green_lantern_switch_deko` compilou e empacotou o NRO real do jogo.
O parser PM4 agora despacha Draw/Resolve/Swap para `deko::GameRenderer` neste
target; a apresentação é própria e a criação do dispositivo Vulkan é pulada.
O target Vulkan existente foi preservado. O novo executável ainda liga
componentes comuns do SDK e pode conter símbolos Vulkan.

Pacote local: `outputs/green-lantern-deko-package/`, com NRO, manifesto,
122 variantes DKSH do jogo, 77 módulos auxiliares e rollback Vulkan 074.
Copiar a pasta `switch` inteira; `deko_shaders` é obrigatória.
Build final: `outputs/deko-native/build-nro-final.log`.
Validação estrutural e hashes: `outputs/green-lantern-deko-package/validacao.json`.

Conectados: ABI de constantes, shaders AOT por hash/modificação, fetches de
vértices relocados, texturas/índices, raster/blend/depth/stencil, auxiliares de
primitivas, apresentação/gamma, resolves de cor e profundidade, aliases de
cor e restauração de stencil dos padrões conhecidos do renderer Vulkan.
Heap 2000 MiB e backing 384 MiB preservados; vértices 160 MiB, uploads 3x32 MiB.

**Ainda sem execução GPU deko3d ou FPS medido no Switch.** Os testes PC
anteriores validaram CPU/ABI e replay Vulkan, não este backend deko3d.
Cobertura AOT limitada às variantes capturadas; variantes ausentes são
logadas/ignoradas. Texturas 3D, alguns formatos e conversões gerais de
EDRAM/MSAA e GS com clip/cull continuam limitados. Não declarar equivalência
visual ou ganho de desempenho antes do teste no console.

As seções abaixo são histórico das etapas anteriores; os avisos antigos
de "PM4 não conectado"/"sem NRO" descrevem aqueles momentos, não o estado atual.

---

'''
for path in (project / 'switch/PORT_STATUS.md', project / 'switch/deko_experiment/README.md'):
    previous = path.read_text(encoding='utf-8-sig')
    path.write_text(status + previous, encoding='utf-8')

archive = package.with_suffix('.zip')
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=3) as out:
    for path in package.rglob('*'):
        if path.is_file():
            out.write(path, path.relative_to(package))
print(json.dumps({'nro': str(nro), 'sha256': digest(nro), 'bytes': len(data), 'dksh_modules': len(dksh), 'zip': str(archive), 'zip_bytes': archive.stat().st_size}, indent=2))
