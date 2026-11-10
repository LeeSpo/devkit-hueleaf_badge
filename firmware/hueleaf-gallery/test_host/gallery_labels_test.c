/* Exercise production label parsing and both wire formats with bounded files. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static uint8_t label_blob[1900];
static size_t label_size;
static FILE *label_open(const char *path)
{
    if (strstr(path, "missing.lbl")) return NULL;
    FILE *f = tmpfile(); assert(f);
    assert(fwrite(label_blob, 1, label_size, f) == label_size);
    rewind(f); return f;
}
#define GALLERY_LABEL_IO 1
#define main existing_gallery_tests
#define GALLERY_SOURCE "../main/gallery.c"
#include "gallery_test.c"
#undef main

static void make_label(const uint8_t *text, size_t n)
{
    assert(n <= 255);
    memcpy(label_blob, "HLL1", 4); label_blob[4] = n;
    memcpy(label_blob + 5, text, n);
    /* A valid 8x1 bitmap. */
    memcpy(label_blob + 5 + n, (uint8_t[]){8, 0, 1, 0, 0xaa}, 5);
    label_size = 10 + n;
}
static uint64_t page_token(const uint8_t *p)
{ uint64_t v = 0; for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i); return v; }
static void check_label(const uint8_t *text, size_t n)
{
    uint8_t page[506]; size_t used;
    assert(gallery_selection_labels_read(0, 0, page, sizeof(page), &used) == ESP_OK);
    size_t off = 17 + strlen(s_names[0]);
    assert(page[12] == 1 && page[13] == 0 && page[off] == n);
    assert(used == off + 1 + n && !memcmp(page + off + 1, text, n));
}
int main(void)
{
    assert(gallery_prepare() == ESP_OK);
    s_n = 1; strcpy(s_names[0], "img-abc123.bin"); s_selected[0] = true;
    const uint8_t mixed[] = "夏日花园 Garden 🌿";
    make_label(mixed, sizeof(mixed) - 1); check_label(mixed, sizeof(mixed) - 1);
    /* No Unicode sequence gets shortened at the 255-byte boundary. */
    uint8_t longest[255];
    for (int i = 0; i < 255; i += 3) memcpy(longest + i, "花", 3);
    make_label(longest, sizeof(longest)); check_label(longest, sizeof(longest));
    uint8_t page[506], old[506]; size_t used, old_used;
    assert(gallery_selection_read(0, 0, old, sizeof(old), &old_used) == ESP_OK);
    assert(gallery_selection_labels_read(0, 0, page, sizeof(page), &used) == ESP_OK);
    assert(!memcmp(old, page, 15) && old_used == 17 + strlen(s_names[0]));
    assert(!memcmp(old + 15, page + 15, old_used - 15));
    uint64_t token = page_token(page);
    make_label((const uint8_t *)"new", 3);
    /* Label-only edits are live metadata and do not invalidate the token. */
    assert(gallery_selection_labels_read(0, token, page, sizeof(page), &used) == ESP_OK);
    check_label((const uint8_t *)"new", 3);
    make_label((const uint8_t *)"  \t", 3); check_label((const uint8_t *)"  \t", 3);
    make_label((const uint8_t *)"", 0); check_label((const uint8_t *)"", 0);
    const uint8_t bad[][4] = {{0xc0,0xaf}, {0xed,0xa0,0x80}, {0xf4,0x90,0x80,0x80}, {0xe8}, {'a',0,'b'}, {1}};
    const size_t lengths[] = {2,3,4,1,3,1};
    for (size_t i = 0; i < sizeof(lengths)/sizeof(lengths[0]); i++) {
        make_label(bad[i], lengths[i]); check_label((const uint8_t *)"", 0);
    }
    make_label(mixed, sizeof(mixed) - 1); label_size--; check_label((const uint8_t *)"", 0);
    make_label(mixed, sizeof(mixed) - 1); label_blob[0] = 'X'; check_label((const uint8_t *)"", 0);
    make_label(mixed, sizeof(mixed) - 1); label_blob[5 + sizeof(mixed) - 1] = 0; check_label((const uint8_t *)"", 0);
    label_size = 4; check_label((const uint8_t *)"", 0);
    strcpy(s_names[0], "missing.bin"); check_label((const uint8_t *)"", 0);
    /* Worst record: 63 filename bytes + 255 label bytes, always fits BLE. */
    memset(s_names[0], 'a', 59); strcpy(s_names[0] + 59, ".bin");
    make_label(longest, 255); check_label(longest, 255);
    assert(gallery_selection_labels_read(0, 0, page, 20, &used) == ESP_ERR_INVALID_ARG);
    /* 300 long labels cross pages; selection and filename identity survive. */
    s_n = GALLERY_MAX;
    for (int i = 0; i < s_n; i++) { snprintf(s_names[i], GALLERY_NAME_MAX, "img-%03d.bin", i); s_selected[i] = i % 2; }
    int start = 0, pages = 0; token = 0;
    while (start < s_n) {
        assert(gallery_selection_labels_read(start, token, page, sizeof(page), &used) == ESP_OK);
        assert(used <= 506 && page[8] == 44 && page[9] == 1);
        assert((page[10] | page[11] << 8) == start);
        int count = page[12] | page[13] << 8; assert(count > 0);
        size_t off = 15;
        for (int i = 0; i < count; i++) {
            assert(page[off++] == (start + i) % 2);
            size_t n = page[off++]; assert(n == strlen(s_names[start + i]));
            assert(!memcmp(page + off, s_names[start + i], n)); off += n;
            assert(page[off++] == 255 && !memcmp(page + off, longest, 255)); off += 255;
        }
        assert(off == used); token = page_token(page); start += count; pages++;
    }
    assert(pages > 1);
    s_selected[0] = !s_selected[0];
    assert(gallery_selection_labels_read(1, token, page, sizeof(page), &used) == ESP_ERR_INVALID_STATE);
    assert(gallery_selection_labels_read(301, 0, page, sizeof(page), &used) == ESP_ERR_INVALID_STATE);
    s_n = 0;
    assert(gallery_selection_labels_read(0, 0, page, sizeof(page), &used) == ESP_OK && used == 15);
    puts("OK labeled inventory, UTF-8, corruption, legacy compatibility and 300-image paging");
    return 0;
}
