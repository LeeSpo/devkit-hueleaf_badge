/* Focused normal-startup policy: persist the displayed frame identity and
 * retain it when the photograph and display settings are unchanged. */
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

    /* A status bar is a snapshot, not a reason to repaint an unchanged photo. */
    before = shows;
    assert(gallery_restore_remote("one.bin") == ESP_OK && shows == before);
    assert(gallery_render_scheduled("one.bin") == ESP_OK && shows == before);
    s_selected[0] = false;
    assert(gallery_render_scheduled("one.bin") == ESP_OK && shows == before);
    s_selected[0] = true;
    int completed;
    assert(runtime_display_begin()); render_picker(0, 1, s_names);
    assert(!saved_display_valid);
    before = shows;
    gallery_boot_init(false); assert(gallery_init() == ESP_OK); s_strip = true;
    assert(gallery_retain_initial() == ESP_OK && shows == before + 1);
    /* Same-photo restoration never emits a new-photo timing callback. */
    before = shows; completed = frame_ok;
    assert(gallery_restore_remote("one.bin") == ESP_OK && shows == before && frame_ok == completed);
    display_result = ESP_FAIL;
    s_view.magic = 0;
    assert(render_index(0, 1, "one.bin", NULL) == ESP_FAIL && !saved_display_valid);
    display_result = ESP_OK;
    gallery_boot_init(false); assert(gallery_init() == ESP_OK);
    before = shows;
    assert(gallery_retain_initial() == ESP_OK && shows == before + 1);

    s_strip = false;
    frame_byte ^= 0x33;
    before = shows;
    assert(gallery_retain_initial() == ESP_OK && shows == before + 1);

    before = shows;
    nvs_commit_failure = true; s_view.magic = 0;
    assert(render_index(0, 1, "one.bin", NULL) == ESP_FAIL && shows == before);
    nvs_commit_failure = false;
    puts("OK gallery startup: NVS retention, status refresh and same-name update");
    return 0;
}
