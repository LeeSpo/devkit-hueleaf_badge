#include <stdio.h>
#include <string.h>
#include <assert.h>
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
    const char *integers = "\xEF\xBB\xBFslide=1\r\nSLIDE = 10080 # longest\nslide=-1\nslide=10081\nslide=999999999999999999999999999\n";
    assert(hl_cfg_get_int(integers, strlen(integers), "slide", -1, 1, 10080) == 10080);
    const char *bad[] = {"slide=0", "slide=+1", "slide=1.5", "slide=1e2", "slide=", "slide=12junk"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        assert(hl_cfg_get_int(bad[i], strlen(bad[i]), "slide", 60, 1, 10080) == 60);
    char out[512], again[512];
    const char *source = "\xEF\xBB\xBF# custom\r\nmono_fast=0\r\n SLIDE = 5  # keep\r\nslide=9\nunknown=yes";
    size_t n = hl_cfg_set_value(source, strlen(source), "slide", "60", out, sizeof(out));
    const char *want = "\xEF\xBB\xBF# custom\r\nmono_fast=0\r\n SLIDE = 60  # keep\r\nslide=60\nunknown=yes";
    assert(n == strlen(want) && !strcmp(out, want));
    assert(hl_cfg_set_value(out, n, "slide", "60", again, sizeof(again)) == n && !strcmp(out, again));
    assert(hl_cfg_set_value("", 0, "slide", "1", out, sizeof(out)) == 8 && !strcmp(out, "slide=1\n"));
    assert(hl_cfg_set_value("other=0", 7, "slide", "1", out, sizeof(out)) == 16 && !strcmp(out, "other=0\nslide=1\n"));
    assert(!hl_cfg_set_value(source, strlen(source), "slide", "60", out, 5));
    printf(fails ? "hl_cfg FAIL\n" : "OK hl_cfg parser\n");
    return fails;
}
