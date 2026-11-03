#!/usr/bin/env bash
# Build artifacts after `idf.py build`:
#   dist/install/  bootloader, partition table, otadata(ota_0), gallery app + flash_install.sh
#                  -> keeps stock app at 0x10000 and stock NVS (settings) untouched
#   dist/hueleaf_full_0x0.bin  bootloader+ptable+STOCK app+otadata+gallery, write at 0x0
#                  (for a blank/erased chip; erases NVS/settings because gaps are 0xFF)
set -euo pipefail
cd "$(dirname "$0")/.."
python3 tools/hl_preflight.py image-size build/hueleaf_gallery.bin --expect-file-size
OFFICIAL=../../examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin
B=build; D=dist; I=$D/install
rm -rf "$D"; mkdir -p "$I"
cp $B/bootloader/bootloader.bin $B/partition_table/partition-table.bin $I/
cp $B/hueleaf_gallery.bin $I/gallery_ota0.bin
python3 tools/mk_otadata.py $I/otadata_ota0.bin
python3 tools/extract_stock_app.py "$OFFICIAL" $D/stock_app.bin
cp tools/hl_preflight.py tools/flash_install.py $I/
cat > $I/flash_install.sh <<'S'
#!/usr/bin/env bash
# Usage: ./flash_install.sh /dev/ttyACM0
# Backs up the whole flash and checks the stock app fits BEFORE writing;
# never touches 0x10000 (stock app) or NVS.
set -e; P=${1:?port}; cd "$(dirname "$0")"
PY=$(command -v python3 || command -v python)
exec "$PY" flash_install.py "$P"
S
printf '@echo off\r\nrem Usage: flash_install.bat COM5\r\ncd /d "%%~dp0"\r\npython flash_install.py %%1\r\nexit /b %%ERRORLEVEL%%\r\n' > $I/flash_install.bat
chmod +x $I/flash_install.sh $I/flash_install.py
python -m esptool --chip esp32c5 merge_bin -o $D/hueleaf_full_0x0.bin \
  --flash_mode dio --flash_size 4MB \
  0x2000 $B/bootloader/bootloader.bin 0x8000 $B/partition_table/partition-table.bin \
  0x10000 $D/stock_app.bin 0x180000 $I/otadata_ota0.bin 0x190000 $B/hueleaf_gallery.bin
ls -l $D $I
