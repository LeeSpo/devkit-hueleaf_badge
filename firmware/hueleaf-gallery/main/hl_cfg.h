#pragma once
#include <stddef.h>
/* /sdcard/hueleaf.cfg: "key = value" lines, '#' comments, CRLF/whitespace ok.
 * Returns 0/1 for key with value 0/1 (also false/true, off/on, no/yes),
 * else dflt. Last occurrence wins. Pure C, host-tested. */
#define HL_CFG_PATH "/sdcard/hueleaf.cfg"
int hl_cfg_get_bool(const char *text, size_t len, const char *key, int dflt);
