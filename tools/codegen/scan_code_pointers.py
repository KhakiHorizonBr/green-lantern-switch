import json
import re
import struct
from collections import Counter
from pathlib import Path

root = Path(__file__).resolve().parent
pe_path = root / "extracted" / "default_pe.bin"
partition_path = root / "project" / "generated" / "hinted" / "codegen.partition.json"
data = pe_path.read_bytes()

pe = struct.unpack_from("<I", data, 0x3C)[0]
if data[pe:pe + 4] != b"PE\0\0":
    raise SystemExit("default_pe.bin is not a PE image")
number_sections = struct.unpack_from("<H", data, pe + 6)[0]
optional_size = struct.unpack_from("<H", data, pe + 20)[0]
optional = pe + 24
magic = struct.unpack_from("<H", data, optional)[0]
image_base = struct.unpack_from("<I", data, optional + 28)[0] if magic == 0x10B else struct.unpack_from("<Q", data, optional + 24)[0]
section_table = optional + optional_size
sections = []
for i in range(number_sections):
    off = section_table + i * 40
    name = data[off:off + 8].split(b"\0", 1)[0].decode("ascii", "replace")
    virtual_size, rva, raw_size, raw_off = struct.unpack_from("<IIII", data, off + 8)
    characteristics = struct.unpack_from("<I", data, off + 36)[0]
    sections.append((name, rva, virtual_size, raw_off, raw_size, characteristics))

with partition_path.open("r", encoding="utf-8") as f:
    registered = {int(x, 16) for x in json.load(f)["assignments"]}
lo, hi = 0x82140000, 0x827758EC
function_starts = set()
for name, rva, vs, raw, rs, chars in sections:
    if name == ".pdata":
        for off in range(raw, min(raw + rs, len(data)) - 7, 8):
            start = struct.unpack_from(">I", data, off)[0]
            if lo <= start < hi:
                function_starts.add(start)
exec_ranges = [(image_base + rva, image_base + rva + virtual_size) for name, rva, virtual_size, _, _, flags in sections if flags & 0x20000000]
hits_abs = Counter()
hits_big = Counter()
hits_big_sections = {}
for name, rva, virtual_size, raw_off, raw_size, characteristics in sections:
    if characteristics & 0x20000000:  # executable code section
        continue
    stop = min(raw_off + raw_size, len(data))
    start = raw_off
    for off in range(start + ((-start) & 3), stop - 3, 4):
        value = struct.unpack_from("<I", data, off)[0]
        value_be = struct.unpack_from(">I", data, off)[0]
        if value % 4 == 0 and any(start <= value < end for start, end in exec_ranges) and value not in registered:
            hits_abs[value] += 1
        if value_be % 4 == 0 and any(start <= value_be < end for start, end in exec_ranges) and value_be not in registered:
            hits_big[value_be] += 1
            hits_big_sections.setdefault(name, Counter())[value_be] += 1

print(f"PE image base: 0x{image_base:X}")
print("Sections:")
for name, rva, vs, raw, rs, chars in sections:
    print(f"  {name:8} RVA=0x{rva:08X} raw=0x{raw:08X}+0x{rs:X} flags=0x{chars:08X}")
print(f".pdata function starts: {len(function_starts)}")
for addr in (0x822D5D40, 0x823697D0, 0x82295960):
    print(f"  0x{addr:08X}: {'listed' if addr in function_starts else 'not in .pdata'}")
for label, hits in (("little-endian", hits_abs), ("big-endian", hits_big)):
    print(f"Aligned {label} absolute code-pointer candidates in non-executable sections: {sum(hits.values())} occurrences, {len(hits)} unique values")
    if label == "big-endian":
        in_pdata = set(hits) & function_starts
        print(f"  Of these, {len(in_pdata)} match .pdata function starts; {len(set(hits) - in_pdata)} do not")
        for section_name, section_hits in hits_big_sections.items():
            print(f"  {section_name}: {sum(section_hits.values())} refs / {len(section_hits)} candidates")
    for guest, count in hits.most_common(60):
        print(f"  0x{guest:08X}: {count}")
recomp_files = list((root / "project" / "generated" / "hinted").glob("green_lantern_recomp.*.cpp"))
code = "\n".join(path.read_text(encoding="utf-8", errors="ignore") for path in recomp_files)
label_hits = [addr for addr in hits_big if f"loc_{addr:08X}:" in code]
print(f"Candidates already present as generated local labels: {len(label_hits)} / {len(hits_big)}")
for addr in label_hits[:80]:
    print(f"  0x{addr:08X}")
repeated = [(addr, count) for addr, count in hits_big.items() if count > 1 and addr not in label_hits]
print(f"Repeated pointer candidates not already local labels: {len(repeated)}")
for addr, count in sorted(repeated, key=lambda item: (-item[1], item[0])):
    print(f"  0x{addr:08X}: {count}")
manifest_text = (root / "project" / "green_lantern_manifest.toml").read_text(encoding="utf-8")
already_hinted = {int(value, 16) for value in re.findall(r'\[entrypoint\.functions\."0x([0-9A-Fa-f]+)"\]', manifest_text)}
batch = sorted(set(hits_big) - set(label_hits) - already_hinted)
print(f"New code-pointer hints not already registered, local labels, or in manifest: {len(batch)}")
print(" ".join(f"0x{addr:08X}" for addr in batch))
if batch and False:  # Enable only when intentionally creating another bulk hint batch.
    with (root / "project" / "green_lantern_manifest.toml").open("a", encoding="utf-8") as manifest:
        for addr in batch:
            manifest.write(f'\n[entrypoint.functions."0x{addr:08X}"]\nname = "manual_{addr:08X}"\n')
    print("Appended this batch to green_lantern_manifest.toml")
