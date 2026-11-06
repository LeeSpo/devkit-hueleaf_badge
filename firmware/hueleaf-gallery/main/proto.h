#pragma once
#include "esp_err.h"
/* Framed host protocol over USB-Serial-JTAG (no console on that port).
 * Frame: "HLP1" | cmd u8 | seq u8 | flags u16 | len u32 LE | payload[len] |
 *        crc32 u32 LE (zlib CRC-32 over cmd..payload, i.e. bytes 4..12+len).
 * Reply: same frame, cmd|0x80, payload = status i32 LE + data.
 * status: 0 ok, -1 error, -2 bad CRC (resend), -3 unknown cmd, -4 not found,
 *         -5 bad arg, -6 no TF card, -7 io error. The host resends on timeout;
 * DATA chunks are idempotent by offset. Bytes outside frames are ignored, so
 * ROM/bootloader boot text can't desync the stream. */
#define PROTO_MAX_PAYLOAD 8192
enum {
    CMD_PING = 0x00, CMD_INFO = 0x01, CMD_LIST = 0x02,
    CMD_UP_BEGIN = 0x03, /* u8 kind(0 image,1 firmware,2 name label for image name[]) u32 size u32 crc32 name[] */
    CMD_UP_DATA = 0x04,  /* u32 offset, bytes */
    CMD_UP_END = 0x05,   /* commit to TF; update list without refreshing the screen */
    CMD_DELETE = 0x06, CMD_SHOW = 0x07, /* name[] */
    CMD_NAV = 0x08,      /* i8 delta */
    CMD_LOG = 0x09, CMD_REBOOT = 0x0A, CMD_SET_TIME = 0x0B, /* u16 y,u8 mo,d,h,mi,s */
    CMD_STRIP = 0x0C,    /* u8 on */
    CMD_BOOT_STOCK = 0x0D, /* one-shot reboot into stock factory app */
    CMD_LABELS = 0x0F,     /* -> "card\tdisplay name\n" lines */
    CMD_SET_ORDER = 0x0E,  /* names separated by '\n' -> img/index.txt; empty = delete index (alphabetical) */
};
esp_err_t proto_start(void);
unsigned proto_crc32(unsigned crc, const void *data, unsigned len);
