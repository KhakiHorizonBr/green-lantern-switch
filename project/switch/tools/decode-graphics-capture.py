"""Converte capturas locais de passes em PNGs e mede amostras sem alterar os dados brutos.

Uso: python decode-graphics-capture.py PASTA_DA_CAPTURA
Dependência: Pillow. Os arquivos são recursos do jogo e devem permanecer locais.
"""
import argparse
import json
import math
import struct
from pathlib import Path
from PIL import Image

# Vulkan VkFormat: representação por texel (sem aplicar gamma, swizzle guest ou bias).
FORMATS = {
    37: ('4B', 255), 43: ('4B', 255), 44: ('4B', 255), 50: ('4B', 255),
    70: ('H', 65535), 76: ('e', 1), 78: ('2h', 32767), 83: ('2e', 1),
    92: ('4h', 32767), 97: ('4e', 1), 100: ('f', 1), 103: ('2f', 1),
}

def decode(meta_path):
    meta = json.loads(meta_path.read_text())
    data = meta_path.with_suffix('.bin').read_bytes()
    width, height, fmt = meta['width'], meta['height'], meta['vk_format']
    if len(data) != meta['bytes']:
        raise ValueError('arquivo incompleto')
    if fmt == 64:
        size = 4
        def pixel(offset):
            word, = struct.unpack_from('<I', data, offset)
            return [(word >> shift & 1023) / 1023 for shift in (0, 10, 20)] + [
                (word >> 30) / 3]
    else:
        layout, divisor = FORMATS[fmt]
        unpack = struct.Struct('<' + layout)
        size = unpack.size
        def pixel(offset):
            values = [max(-1, v / divisor) if divisor == 32767 else v / divisor
                      for v in unpack.unpack_from(data, offset)]
            if fmt in (44, 50):
                values[0], values[2] = values[2], values[0]
            return values
    if meta['row_bytes'] != width * size or len(data) != width * height * size:
        raise ValueError('layout incompatível')
    step = max(1, math.ceil(max(width, height) / 640))
    pixels = []
    minimum, maximum = [math.inf] * 4, [-math.inf] * 4
    nonfinite = below = above = samples = 0
    for y in range(0, height, step):
        for x in range(0, width, step):
            values = pixel((y * width + x) * size)
            for channel, value in enumerate(values):
                samples += 1
                if not math.isfinite(value):
                    nonfinite += 1
                    continue
                minimum[channel] = min(minimum[channel], value)
                maximum[channel] = max(maximum[channel], value)
                below += value < 0
                above += value > 1
            rgb = values[:3] if len(values) >= 3 else values[:1] * 3
            pixels.append(tuple(round(max(0, min(1, value)) * 255)
                                if math.isfinite(value) else 255 for value in rgb))
    image = Image.new('RGB', (math.ceil(width / step), math.ceil(height / step)))
    image.putdata(pixels)
    image.save(meta_path.with_suffix('.png'))
    return dict(file=meta_path.name, **meta, sampled_channels=samples,
                sampled_nonfinite=nonfinite, sampled_below_zero=below,
                sampled_above_one=above,
                sampled_min=[v if math.isfinite(v) else None for v in minimum],
                sampled_max=[v if math.isfinite(v) else None for v in maximum])

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('folder', type=Path)
    args = parser.parse_args()
    results = []
    for path in sorted(args.folder.glob('pass_*.json')):
        try:
            results.append(decode(path))
        except (ValueError, KeyError, OSError, struct.error) as error:
            results.append(dict(file=path.name, error=str(error)))
    (args.folder / 'capture-summary.json').write_text(
        json.dumps(results, indent=2, ensure_ascii=False), encoding='utf-8')
    print(f'{len(results)} passes processados; consulte capture-summary.json')

if __name__ == '__main__':
    main()
