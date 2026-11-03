#include <stdio.h>
#include <string.h>
#include "hl_cfg.h"
static int fails;
static void t(const char *s, int d, int want)
{
    int got = hl_cfg_get_bool(s, strlen(s), "mono_fast", d);
    if (got != want) { printf("FAIL [%s] got %d want %d\n", s, got, want); fails++; }
}
int main(void)
{
    t("", 1, 1);
    t("mono_fast=0", 1, 0);
    t("  mono_fast   =   0  \r\n", 1, 0);
    t("\xEF\xBB\xBFmono_fast=0\r\n", 1, 0);
    t("# mono_fast=0\n", 1, 1);
    t("mono_fast=0 # off for now\n", 1, 0);
    t("mono_fast=1", 1, 1);
    t("mono_fast=0\nmono_fast=1\n", 1, 1);
    t("MONO_FAST=OFF", 1, 0);
    t("mono_fastx=0\nxmono_fast=0", 1, 1);
    t("mono_fast=garbage", 1, 1);
    t("mono_fast\n=0", 1, 1);
    t("\tmono_fast\t=\tfalse\t", 1, 0);
    t("other=1\r\nmono_fast=0", 1, 0);
    t("", -1, -1);                 /* absent -> caller falls back to NVS */
    t("other=1\n", -1, -1);
    t("mono_fast=junk", -1, -1);
    printf(fails ? "hl_cfg FAIL\n" : "OK hl_cfg parser\n");
    return fails;
}
