"""Compara capturas manuais FSI/nativo no PC (dados privados do jogo).

Uso: python compare-renderer-captures.py PASTA_CAPTURE PASTA_RESULTADO
Dependências: numpy, Pillow. Não publica arquivos. As imagens PNG são previews;
a diferença numérica usa valores normalizados sem clamp e só formatos conhecidos.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image

# Formatos com interpretação de armazenamento equivalente conhecida. Outros são
# explicitamente recusados, especialmente fixed16 de faixa estendida e depth.
NATIVE = {37: ('u1', 4, 255), 43: ('u1', 4, 255),
          64: ('<u4', 1, 1),
          44: ('u1', 4, 255), 50: ('u1', 4, 255),
          70: ('<u2', 1, 65535), 76: ('<f2', 1, 1),
          83: ('<f2', 2, 1), 97: ('<f2', 4, 1),
          100: ('<f4', 1, 1), 103: ('<f4', 2, 1)}
FSI = {6: ('u1', 4, 255), 7: ('<u4', 1, 1), 14: ('u1', 4, 255),
       22: ('<u4', 1, 1), 23: ('<u4', 1, 1), 24: ('<u2', 1, 65535),
       30: ('<f2', 1, 1), 31: ('<f2', 2, 1), 32: ('<f2', 4, 1),
       36: ('<f4', 1, 1), 37: ('<f4', 2, 1)}
ENDIAN_XOR = {0: 0, 1: 1, 2: 3, 3: 2, 4: 7, 5: 15}


def unpack_2101010(values):
    words = values[..., 0]
    return np.stack([(words & 1023) / 1023, ((words >> 10) & 1023) / 1023,
                     ((words >> 20) & 1023) / 1023, (words >> 30) / 3], axis=-1)


def decode(path, meta):
    is_fsi = meta['backend'] == 'fsi'
    blob = path.with_suffix('.pixels.bin' if is_fsi else '.bin').read_bytes()
    if len(blob) != meta['bytes']:
        raise ValueError('bytes incompletos')
    if is_fsi:
        if meta.get('array') or (meta.get('depth') and meta['dest_format'] not in (22, 23)):
            raise ValueError('depth não reconhecido/array ainda não suportado')
        if meta['dest_format'] not in FSI:
            raise ValueError(f"formato de destino Xenos ainda não suportado: {meta['dest_format']}")
        dtype, channels, divisor = FSI[meta['dest_format']]
        x0, y0, width, height = meta['rect']
        pitch = meta['pitch']
        if not (0 < width <= 8192 and 0 < height <= 8192 and pitch > 0):
            raise ValueError('geometria fora do limite do decodificador')
        bpp = np.dtype(dtype).itemsize * channels
        log2 = bpp.bit_length() - 1
        y, x = np.indices((height, width), dtype=np.int64)
        x += x0
        y += y0
        # Mesmo endereçamento 2D de pipeline/texture/util.cpp (sem array/3D).
        macro = ((x >> 5) + (y >> 5) * ((pitch + 31) >> 5)) << (log2 + 7)
        micro = ((x & 7) + ((y & 14) << 2)) << log2
        offset = macro + ((micro & ~15) << 1) + (micro & 15) + ((y & 1) << 4)
        address = ((offset & ~511) << 3) + ((y & 16) << 7) + ((offset & 448) << 2)
        address += (((((y & 8) >> 2) + (x >> 3)) & 3) << 6) + (offset & 63)
        # Endian128 permuta endereços absolutos; o blob pode começar depois
        # do início da textura. Não assumir que written_address == copy_base.
        physical = address + meta['adjusted_base']
        start = int(meta['address'], 16)
        data = np.frombuffer(blob, dtype='u1')
        unpacked = np.zeros((height, width, bpp), dtype='u1')
        valid = np.ones((height, width), dtype=bool)
        for byte in range(bpp):
            indices = ((physical + byte) ^ ENDIAN_XOR[meta['endian']]) - start
            good = (indices >= 0) & (indices < len(data))
            valid &= good
            unpacked[..., byte][good] = data[indices[good]]
        values = unpacked.view(dtype).reshape(height, width, channels)
        if meta['dest_format'] == 7:
            values = unpack_2101010(values)
        elif meta.get('depth') and meta['dest_format'] in (22, 23):
            # Xenos depth words are depth24 in bits8..31 and stencil in0..7.
            depth24 = values[..., 0] >> 8
            if meta['dest_format'] == 22:
                depth = depth24.astype(np.float32) / 16777215
            else:
                # xenos::Float20e4To32: normal exponent bias15, 20 mantissa bits.
                exponent = (depth24 >> 20).astype(np.int32)
                mantissa = depth24 & 0xFFFFF
                depth = np.where(exponent != 0,
                    np.ldexp(1 + mantissa.astype(np.float64) / 1048576, exponent - 15),
                    mantissa.astype(np.float64) / 17179869184)
            values = depth[..., None]
    else:
        if meta['vk_format'] not in NATIVE:
            raise ValueError(f"formato Vulkan ainda não suportado: {meta['vk_format']}")
        dtype, channels, divisor = NATIVE[meta['vk_format']]
        width, height = meta['width'], meta['height']
        if len(blob) != width * height * channels * np.dtype(dtype).itemsize:
            raise ValueError('layout linear incompleto')
        values = np.frombuffer(blob, dtype=dtype).reshape(height, width, channels)
        if meta['vk_format'] == 64:
            values = unpack_2101010(values)
        if meta['vk_format'] in (44, 50):
            values = values[..., [2, 1, 0, 3]]
        # Native keeps copy_dest_swap in the sampled image view, while FSI
        # physically encodes it into the guest resolve bytes. Compare the
        # equivalent RGBA8 representation, not storage versus swizzled storage.
        if meta.get('role') == 'destination' and meta['vk_format'] in (37, 43, 64):
            registers = np.fromfile(path.with_suffix('.registers.bin'), dtype='<u4')
            if len(registers) <= 0x231B:
                raise ValueError('registradores ausentes para normalizar copy_dest_swap')
            if (int(registers[0x231B]) >> 24) & 1:
                values = values[..., [2, 1, 0, 3]]
        x, y, w, h = meta['rect']
        if not (w > 0 and h > 0 and x >= 0 and y >= 0 and x+w <= width and y+h <= height):
            raise ValueError('retângulo não coberto pela imagem linear')
        values = values[y:y+h, x:x+w]
        valid = np.ones(values.shape[:2], dtype=bool)
    return values.astype(np.float32) / divisor, valid


def preview(path, values, valid):
    rgb = values[..., :3] if values.shape[-1] >= 3 else np.repeat(values[..., :1], 3, axis=-1)
    rgb = (np.clip(np.nan_to_num(rgb), 0, 1) * 255).astype('u1')
    rgb[~valid] = (255, 0, 255)
    Image.fromarray(rgb).save(path)


def compare_inputs(native, fsi):
    results = []
    for p in sorted((native).glob('draw_*.json')):
        a = json.loads(p.read_text())
        ar = np.fromfile(p.with_suffix('.registers.bin'), dtype='<u4')
        candidates = []
        for q in fsi.glob('draw_*.json'):
            b = json.loads(q.read_text())
            if (a['vs'], a['ps']) != (b['vs'], b['ps']):
                continue
            br = np.fromfile(q.with_suffix('.registers.bin'), dtype='<u4')
            if ar.shape != br.shape:
                continue
            regions = {'render': (0x2000, 0x2400), 'fetch': (0x4800, 0x48C0),
                       'float_constants': (0x4000, 0x4800)}
            diff = {k: np.flatnonzero(ar[lo:hi] != br[lo:hi]) + lo for k, (lo, hi) in regions.items()}
            candidates.append(dict(file=str(q), counts={k: int(len(v)) for k, v in diff.items()},
                                   first_registers={k: [f'{i:04X}' for i in v[:16]] for k, v in diff.items()}))
        candidates.sort(key=lambda c: c['counts']['render'] + c['counts']['fetch'])
        results.append(dict(native=str(p), vs=a['vs'], ps=a['ps'], candidates=candidates[:3]))
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--fsi-source', type=Path,
                        help='Read the preserved FSI reference directly without duplicating raw captures')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'limits': ['Capturas independentes: animação/tempo não são sincronizados.',
                        'Um resolve diferente pode ser consequência de draw anterior.',
                        'Não comparar estruturas binárias de system constants entre backends.',
                        'Formatos desconhecidos são omitidos, nunca tratados como RGBA8.'],
              'surfaces': [], 'pairs': []}
    surfaces = {'native': [], 'fsi': []}
    for backend in surfaces:
        source_dir = args.fsi_source if backend == 'fsi' and args.fsi_source else args.capture/backend
        for path in sorted(source_dir.glob('*.json')):
            if path.name.startswith('draw_'):
                continue
            try:
                m = json.loads(path.read_text())
                if m.get('role') != 'destination':
                    continue
                values, valid = decode(path, m)
                name = backend + '_' + path.stem + '.png'
                preview(args.output/name, values, valid)
                report['surfaces'].append(dict(file=str(path), preview=name, shape=list(values.shape),
                    valid_pixels=int(valid.sum()), pixels=int(valid.size),
                    nonfinite=int((~np.isfinite(values)).sum())))
                surfaces[backend].append((path, m))
            except (OSError, ValueError, KeyError) as e:
                report['surfaces'].append(dict(file=str(path), skipped=str(e)))
    for p, a in surfaces['native']:
        for q, b in surfaces['fsi']:
            if (a['copy_base'] & 0x1fffffff, a['dest_format'], a['rect'][2:]) != (
                    b['copy_base'] & 0x1fffffff, b['dest_format'], b['rect'][2:]):
                continue
            av, am = decode(p, a)
            bv, bm = decode(q, b)
            if av.shape != bv.shape:
                continue
            mask = am & bm & np.isfinite(av).all(axis=-1) & np.isfinite(bv).all(axis=-1)
            if not mask.any():
                continue
            delta = np.abs(av[mask] - bv[mask])
            report['pairs'].append(dict(native=str(p), fsi=str(q),
                same_last_shader=(a['vs'], a['ps']) == (b['vs'], b['ps']),
                compared_pixels=int(mask.sum()), mean_abs_difference=float(delta.mean()),
                max_abs_difference=float(delta.max()), equal_pixels=float((delta == 0).all(axis=-1).mean())))
    report['draw_inputs'] = compare_inputs(args.capture/'native', args.fsi_source or args.capture/'fsi')
    (args.output/'comparison.json').write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f"{len(report['pairs'])} pares candidatos; consulte {args.output/'comparison.json'}")


if __name__ == '__main__':
    main()
