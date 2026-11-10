"""Shared, strict remote-v1 layout contract (no device I/O)."""
LAYOUT_ID = "remote-v1"
ENTRIES = [("nvs", 1, 2, 0x9000, 0x6000), ("phy_init", 1, 1, 0xF000, 0x1000),
           ("otadata", 1, 0, 0x10000, 0x2000), ("remote_nvs", 1, 2, 0x12000, 0xE000),
           ("ota_0", 0, 0x10, 0x20000, 0x1F0000), ("ota_1", 0, 0x11, 0x210000, 0x1F0000)]
FLASH_FILES = {"0x2000": "bootloader/bootloader.bin", "0x8000": "partition_table/partition-table.bin",
               "0x10000": "otadata_ota0.bin", "0x20000": "hueleaf_gallery.bin"}

def matches(table):
    return [tuple(e[:5]) for e in table] == ENTRIES

def migration_allowed(table):
    # Only known NVS/PHY boundaries; reject encrypted and overlapping entries.
    if not table or any(e[5] for e in table):
        return False
    if any(e not in [tuple(x[:5]) for x in table] for e in ENTRIES[:2]):
        return False
    if any(e[3] < 0x10000 and tuple(e[:5]) not in ENTRIES[:2] for e in table):
        return False
    spans = sorted((e[3], e[3] + e[4]) for e in table)
    return all(a >= 0x9000 and a < b <= 0x400000 for a, b in spans) and all(
        spans[i][1] <= spans[i+1][0] for i in range(len(spans)-1))
