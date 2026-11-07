#pragma once
#include <stddef.h>
/* /sdcard/hueleaf.cfg: "key = value" lines, '#' comments, CRLF/whitespace ok.
 * Returns 0/1 for key with value 0/1 (also false/true, off/on, no/yes),
 * else dflt. Last occurrence wins. Pure C, host-tested. */
#define HL_CFG_PATH "/sdcard/hueleaf.cfg"
int hl_cfg_get_bool(const char *text, size_t len, const char *key, int dflt);
/* Strict unsigned decimal, bounded inclusive; invalid values are ignored. */
int hl_cfg_get_int(const char *text, size_t len, const char *key, int dflt,
                   unsigned min, unsigned max);
/* Replace matching values (including duplicates), keeping comments/other lines.
 * Appends a missing key. Returns output length, or 0 if capacity is insufficient. */
size_t hl_cfg_set_value(const char *text, size_t len, const char *key,
                         const char *value, char *out, size_t cap);
