/* Focused normal-startup policy: persist the displayed frame identity and
 * retain it only when the status strip is disabled and the file is unchanged. */
#define GALLERY_STARTUP_TEST
#define GALLERY_SOURCE "../main/gallery.c"
#include "gallery_fixture.h"

int main(void)
{
    assert(gallery_prepare() == ESP_OK);
    card_names[0] = "one.bin";
    card_count = 1;
    strcpy(saved_name, "one.bin");
    saved_index = 0;
    s_strip = false;

    assert(gallery_init() == ESP_OK);
    gallery_boot_init(false);
    assert(gallery_render_initial() == ESP_OK);
    assert(saved_display_valid);

    int before = shows;
    gallery_boot_init(false);
    assert(gallery_init() == ESP_OK);
    s_strip = false;
    assert(gallery_retain_initial() == ESP_OK && shows == before);

    s_strip = true;
    before = shows;
    assert(gallery_retain_initial() == ESP_OK && shows == before + 1);

    s_strip = false;
    frame_byte ^= 0x33;
    before = shows;
    assert(gallery_retain_initial() == ESP_OK && shows == before + 1);

    puts("OK gallery startup: NVS retention, status refresh and same-name update");
    return 0;
}
