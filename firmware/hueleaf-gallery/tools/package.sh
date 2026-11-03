#!/usr/bin/env bash
# Build artifacts after `idf.py build`:
#   dist/install/  bootloader, partition table, otadata(ota_0), gallery app + flash_install.sh
#                  -> keeps stock app at 0x10000 and stock NVS (settings) untouched
#   dist/hueleaf_full_0x0.bin  bootloader+ptable+STOCK app+otadata+gallery, write at 0x0
#                  (for a blank/erased chip; erases NVS/settings because gaps are 0xFF)
set -euo pipefail
cd "$(dirname "$0")/.."
OFFICIAL=../../examples/3.7einkDvb/firmware/3.7einkDvb_factory_0x0.bin
B=build; D=dist; I=$D/install
rm -rf "$D"; mkdir -p "$I"
cp $B/bootloader/bootloader.bin $B/partition_table/partition-table.bin $I/
cp $B/hueleaf_gallery.bin $I/gallery_ota0.bin
python3 tools/mk_otadata.py $I/otadata_ota0.bin
python3 tools/extract_stock_app.py "$OFFICIAL" $D/stock_app.bin
cat > $I/flash_install.sh <<'S'
#!/usr/bin/env bash
# Usage: ./flash_install.sh /dev/ttyACM0   (never touches 0x10000 = stock app)
set -e; P=${1:?port}; cd "$(dirname "$0")"
python -m esptool --chip esp32c5 -p "$P" -b 460800 write_flash \
  0x2000 bootloader.bin 0x8000 partition-table.bin \
  0x180000 otadata_ota0.bin 0x190000 gallery_ota0.bin
S
chmod +x $I/flash_install.sh
python -m esptool --chip esp32c5 merge_bin -o $D/hueleaf_full_0x0.bin \
  --flash_mode dio --flash_size 4MB \
  0x2000 $B/bootloader/bootloader.bin 0x8000 $B/partition_table/partition-table.bin \
  0x10000 $D/stock_app.bin 0x180000 $I/otadata_ota0.bin 0x190000 $B/hueleaf_gallery.bin
ls -l $D $I
