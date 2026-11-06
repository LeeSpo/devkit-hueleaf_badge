#!/usr/bin/env bash
# Package a provenance-verified candidate build without accessing hardware.
# Usage: ./tools/package.sh [BUILD_DIR] [OUTPUT_DIR]
set -euo pipefail
cd "$(dirname "$0")/.."
B=${1:-build}; D=${2:-dist}; I=$D/install
PY=${PYTHON:-python3}
OFFICIAL=../../examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin
"$PY" tools/build_candidate.py --check "$B"
if [ -e "$D" ]; then
    echo "输出目录已存在，拒绝混入旧文件：${D}；请使用新的输出目录。" >&2
    exit 2
fi
"$PY" tools/hl_preflight.py image-size "$B/hueleaf_gallery.bin" --expect-file-size
mkdir -p "$I" "$D/recovery"
cp "$B/bootloader/bootloader.bin" "$B/partition_table/partition-table.bin" "$I/"
cp "$B/hueleaf_gallery.bin" "$I/gallery_ota0.bin"
"$PY" tools/mk_otadata.py "$I/otadata_ota0.bin"
"$PY" tools/extract_stock_app.py "$OFFICIAL" "$D/recovery/stock_app.bin"
cp tools/hl_preflight.py tools/flash_install.py "$I/"
cat > "$I/flash_install.sh" <<'SH'
#!/usr/bin/env bash
set -euo pipefail
PY=$(command -v python3 || command -v python)
exec "$PY" "$(dirname "$0")/flash_install.py" "$@"
SH
printf '@echo off\r\npython "%%~dp0flash_install.py" %%*\r\nexit /b %%ERRORLEVEL%%\r\n' > "$I/flash_install.bat"
chmod +x "$I/flash_install.sh" "$I/flash_install.py"
"$PY" -m esptool --chip esp32c5 merge_bin -o "$D/recovery/hueleaf_full_0x0.bin" \
  --flash_mode dio --flash_size 4MB \
  0x2000 "$I/bootloader.bin" 0x8000 "$I/partition-table.bin" \
  0x10000 "$D/recovery/stock_app.bin" 0x180000 "$I/otadata_ota0.bin" 0x190000 "$I/gallery_ota0.bin"
printf '仅供空白芯片/恢复：完整镜像会覆盖 NVS，日常试版本请用 ../install/flash_install.py。\n' > "$D/recovery/README.txt"
if [ -f docs/release-notes.md ]; then
  cp docs/release-notes.md "$D/README.md"
else
  cp docs/preflash-fixes.md "$D/README.md"
fi
mkdir -p "$D/licenses"
for L in LICENSE LICENSE-APACHE-2.0 LICENSE-CC-BY-NC-SA-4.0; do
  if [ -f "../../$L" ]; then cp "../../$L" "$D/licenses/"; fi
done
"$PY" - "$B" "$D" <<'PY'
import datetime, hashlib, json, pathlib, subprocess, sys
build, dest = map(pathlib.Path, sys.argv[1:])
source = pathlib.Path.cwd().parents[1]
provenance = json.loads((build / 'build_provenance.json').read_text())
config = pathlib.Path(provenance['config_file']).read_bytes()
def git(*args):
    return subprocess.check_output(['git', '-C', str(source), *args]).decode().strip()
record = {'branch': provenance['branch'], 'commit': provenance['base_commit'],
          'source_sha256': provenance['source_sha256'], 'firmware_version': provenance['firmware_version'],
          'dirty': provenance['dirty'], 'built_at': provenance['built_at'],
          'packaged_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
          'sdkconfig_sha256': hashlib.sha256(config).hexdigest(),
          'hardware_validated': False,
          'files': {str(p.relative_to(dest)): hashlib.sha256(p.read_bytes()).hexdigest()
                    for p in sorted(dest.rglob('*')) if p.is_file() and p.name not in ('manifest.json','SHA256SUMS')}}
(dest/'manifest.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n')
(dest/'build_provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
(dest/'SHA256SUMS').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+str(p.relative_to(dest))+'\n'
                            for p in sorted(dest.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
PY
printf '安装包：%s（只生成文件，未连接设备）\n' "$D"
