"""Generate the runtime packer from the validated offline layout corpus."""
import argparse,json
from pathlib import Path

def generate(corpus,output):
    rows=[json.loads(s) for s in corpus.read_text().splitlines()]
    systems=[x for x in rows if x['buffer']=='XeSystemConstants']
    if not systems: raise ValueError('missing system layouts')
    first=systems[0]
    for row in systems:
        if any(row[k]!=first[k] for k in ['source_bytes','destination_bytes','moves']):
            raise ValueError('system layouts differ across variants; per-variant ABI required')
    for row in rows:
        if row['buffer']!='XeSystemConstants' and (row['source_bytes']!=row['destination_bytes'] or
            any(a!=b for a,b,n in row['moves'])):
            raise ValueError('another buffer needs runtime packing: '+row['buffer'])
    if (first['source_bytes'],first['destination_bytes'])!=(504,528):
        raise ValueError('unexpected source ABI; review before regenerating')
    moves=first['moves']
    text='// Generated from the validated constant-moves.jsonl corpus.\n#pragma once\n#include <cstdint>\n#include <cstring>\n#include <cstdlib>\nnamespace green_lantern::deko {\ninline constexpr uint32_t kSystemSourceBytes=504, kSystemDestinationBytes=528;\ninline void PackSystem(const void* source, void* destination) {\n  auto* dst=static_cast<uint8_t*>(destination);\n  const auto* src=static_cast<const uint8_t*>(source);\n  std::memset(dst,0,kSystemDestinationBytes);\n'
    text+=''.join(f'  std::memcpy(dst+{b},src+{a},{n});\n' for a,b,n in moves)
    text+='}\ninline uint32_t SystemOffset(uint32_t offset) {\n'
    text+=''.join(f'  if(offset >= {a}u && offset < {a+n}u) return {b}u + offset - {a}u;\n' for a,b,n in moves)
    text+='  std::abort(); // No unproven offset may reach a shader.\n}\n}\n'
    return text

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('corpus',type=Path);p.add_argument('output',type=Path);p.add_argument('--check',action='store_true');a=p.parse_args()
    result=generate(a.corpus,a.output)
    if a.check:
        if a.output.read_text()!=result:raise SystemExit('generated upload contract is stale')
        print('runtime upload contract matches corpus')
    else:a.output.write_text(result,encoding='utf-8')
