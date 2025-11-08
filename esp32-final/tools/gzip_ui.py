import os, gzip, shutil
from pathlib import Path

SRC = Path('websrc')
DST = Path('data/www')

count = 0
saved = 0

for src_file in SRC.rglob('*'):
    if src_file.is_file():
        rel = src_file.relative_to(SRC)
        dst_file = DST / (str(rel) + '.gz')
        dst_file.parent.mkdir(parents=True, exist_ok=True)
        if dst_file.exists() and dst_file.stat().st_mtime > src_file.stat().st_mtime:
            continue
        with open(src_file, 'rb') as f_in:
            data = f_in.read()
        with gzip.open(dst_file, 'wb', compresslevel=9) as f_out:
            f_out.write(data)
        ratio = 100.0 * (1 - dst_file.stat().st_size / len(data)) if len(data) else 0
        print(f'Gzipped {src_file} -> {dst_file} ({len(data)} -> {dst_file.stat().st_size}, {ratio:.1f}% saved)')
        count += 1
        saved += len(data) - dst_file.stat().st_size
print(f'Total files processed: {count}, total bytes saved: {saved}')
