#include "hl_cfg.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>

static int word_eq(const char *s, size_t n, const char *w)
{
    return strlen(w) == n && strncasecmp(s, w, n) == 0;
}

int hl_cfg_get_bool(const char *text, size_t len, const char *key, int dflt)
{
    int v = dflt;
    size_t i = 0;
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) i = 3;   /* UTF-8 BOM */
    while (i < len) {
        size_t e = i;
        while (e < len && text[e] != '\n' && text[e] != '\r') e++;
        const char *s = text + i, *end = text + e;
        const char *h = memchr(s, '#', (size_t)(end - s));
        if (h) end = h;
        const char *eq = memchr(s, '=', (size_t)(end - s));
        if (eq) {
            const char *ks = s, *ke = eq, *vs = eq + 1, *ve = end;
            while (ks < ke && isspace((unsigned char)*ks)) ks++;
            while (ke > ks && isspace((unsigned char)ke[-1])) ke--;
            while (vs < ve && isspace((unsigned char)*vs)) vs++;
            while (ve > vs && isspace((unsigned char)ve[-1])) ve--;
            if (word_eq(ks, (size_t)(ke - ks), key)) {
                size_t n = (size_t)(ve - vs);
                if (word_eq(vs, n, "0") || word_eq(vs, n, "false") || word_eq(vs, n, "off") || word_eq(vs, n, "no")) v = 0;
                else if (word_eq(vs, n, "1") || word_eq(vs, n, "true") || word_eq(vs, n, "on") || word_eq(vs, n, "yes")) v = 1;
            }
        }
        i = e + 1;
    }
    return v;
}
