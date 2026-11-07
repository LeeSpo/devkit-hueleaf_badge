#include "hl_cfg.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>
#include <stdbool.h>

static bool value_span(const char *s, const char *end, const char *key,
                        const char **vs, const char **ve)
{
    const char *comment = memchr(s, '#', (size_t)(end - s));
    if (comment) end = comment;
    const char *eq = memchr(s, '=', (size_t)(end - s));
    if (!eq) return false;
    const char *ks = s, *ke = eq;
    while (ks < ke && isspace((unsigned char)*ks)) ks++;
    while (ke > ks && isspace((unsigned char)ke[-1])) ke--;
    if (strlen(key) != (size_t)(ke - ks) || strncasecmp(ks, key, ke - ks)) return false;
    *vs = eq + 1; *ve = end;
    while (*vs < *ve && isspace((unsigned char)**vs)) (*vs)++;
    while (*ve > *vs && isspace((unsigned char)(*ve)[-1])) (*ve)--;
    return true;
}

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

int hl_cfg_get_int(const char *text, size_t len, const char *key, int dflt,
                   unsigned min, unsigned max)
{
    int result = dflt;
    size_t i = len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3) ? 3 : 0;
    while (i < len) {
        size_t end = i;
        while (end < len && text[end] != '\n' && text[end] != '\r') end++;
        const char *vs, *ve;
        if (value_span(text + i, text + end, key, &vs, &ve)) {
            unsigned value = 0;
            bool valid = vs < ve;
            for (const char *p = vs; p < ve && valid; p++) {
                unsigned digit = (unsigned char)*p - '0';
                if (digit > 9 || digit > max || value > (max - digit) / 10) valid = false;
                else value = value * 10 + digit;
            }
            if (valid && value >= min) result = (int)value;
        }
        i = end + 1;
    }
    return result;
}

size_t hl_cfg_set_value(const char *text, size_t len, const char *key,
                         const char *value, char *out, size_t cap)
{
    size_t i = 0, n = 0, value_len = strlen(value);
    bool found = false;
    while (i < len) {
        size_t end = i;
        while (end < len && text[end] != '\n' && text[end] != '\r') end++;
        const char *start = text + i, *vs, *ve;
        if (!i && len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3)) start += 3;
        bool match = value_span(start, text + end, key, &vs, &ve);
        size_t line_end = end;
        while (line_end < len && (text[line_end] == '\n' || text[line_end] == '\r')) line_end++;
        size_t need = line_end - i;
        if (match) need = (size_t)(vs - text - i) + value_len + (size_t)(text + line_end - ve);
        if (n + need >= cap) return 0;
        if (match) {
            size_t prefix = (size_t)(vs - text - i);
            memcpy(out + n, text + i, prefix); n += prefix;
            memcpy(out + n, value, value_len); n += value_len;
            size_t suffix = (size_t)(text + line_end - ve);
            memcpy(out + n, ve, suffix); n += suffix;
            found = true;
        } else { memcpy(out + n, text + i, need); n += need; }
        i = line_end;
    }
    if (!found) {
        bool newline = n && out[n - 1] != '\n' && out[n - 1] != '\r';
        size_t key_len = strlen(key);
        if (n + newline + key_len + value_len + 2 >= cap) return 0;
        if (newline) out[n++] = '\n';
        memcpy(out + n, key, key_len); n += key_len; out[n++] = '=';
        memcpy(out + n, value, value_len); n += value_len; out[n++] = '\n';
    }
    out[n] = 0;
    return n;
}
