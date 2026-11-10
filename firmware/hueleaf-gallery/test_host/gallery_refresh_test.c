/* Real gallery worker, file reads, display admission and remote completion. */
#define GALLERY_REFRESH_TEST
#define GALLERY_SOURCE "../main/gallery.c"
#include "gallery_test.c"

int main(void)
{
    assert(gallery_prepare() == ESP_OK);
    card_names[0] = "one.bin"; card_count = 1;
    assert(gallery_init() == ESP_OK);
    gallery_boot_init(false);
    assert(gallery_render_initial() == ESP_OK);
    assert(shown_byte() == frame_byte);
    gallery_set_ready(true);
    assert(gallery_start_task() == ESP_OK);
    int before = shows, notified = notifications, commits = nvs_commits;

    gallery_set_strip(s_strip);
    assert(notifications == notified && !s_force);
    assert(gallery_show_name("one.bin") == ESP_OK);
    run_gallery_once();
    assert(shows == before);
    gallery_nav(1); run_gallery_once();
    assert(shows == before && s_cur == 0);
    gallery_nav(-1); run_gallery_once();
    assert(shows == before && nvs_commits == commits);

    /* Same name, different bytes: rescan/show must still notice the update. */
    frame_byte ^= 0x66;
    gallery_rescan(true); run_gallery_once();
    assert(shows == ++before);
    assert(shown_byte() == frame_byte);
    gallery_rescan(true); run_gallery_once();
    assert(shows == before);
    gallery_set_strip(!s_strip); run_gallery_once();
    assert(shows == ++before);
    gallery_set_strip(s_strip); run_gallery_once();
    assert(shows == before);

    /* Reading the clock/battery alone is not an independent refresh reason. */
    runtime_publish_battery(true, 42);
    assert(gallery_show_name("one.bin") == ESP_OK); run_gallery_once();
    assert(shows == before);

    /* Covered ink must be restored, including selecting the very same file. */
    assert(runtime_display_begin()); render_picker(0, 1, s_names);
    before = shows;
    assert(gallery_show_name("one.bin") == ESP_OK); run_gallery_once();
    assert(shows == ++before);
    static uint8_t ui[FRAME_UI_BYTES];
    assert(gallery_show_ui_frame(ui) == ESP_OK); before = shows;
    assert(gallery_show_name("one.bin") == ESP_OK); run_gallery_once();
    assert(shows == ++before);

    /* A checked deep-sleep frame survives; corrupt/cold state does not. */
    gallery_boot_init(true); gallery_restore_remote("one.bin");
    assert(shows == before && s_cur == 0);
    assert(gallery_show_name("one.bin") == ESP_OK); run_gallery_once();
    assert(shows == before);
    s_view.checksum ^= 1; gallery_boot_init(true);
    assert(gallery_show_name("one.bin") == ESP_OK); run_gallery_once();
    assert(shows == ++before);
    gallery_boot_init(false);
    assert(gallery_show_name("one.bin") == ESP_OK); run_gallery_once();
    assert(shows == ++before);

    /* A failed refresh never establishes a skippable frame. */
    frame_byte ^= 0x33; display_result = ESP_FAIL;
    assert(gallery_show_name("one.bin") == ESP_OK); run_gallery_once();
    assert(shows == ++before && s_view.magic == 0);
    display_result = ESP_OK;
    assert(gallery_show_name("one.bin") == ESP_OK); run_gallery_once();
    assert(shows == ++before && s_view.magic == VIEW_MAGIC);

    /* Real BLE operation guards must finish even when there is no DRF. */
#ifdef CONFIG_HL_REMOTE
    int completed = remote_completions;
#endif
    assert(gallery_remote_request(0, "one.bin") == ESP_OK);
    assert(runtime_remote_operation_pending()); run_gallery_once();
    assert(shows == before && !runtime_remote_operation_pending());
#ifdef CONFIG_HL_REMOTE
    assert(remote_completions == completed + 1 && remote_success);
#endif
    assert(gallery_remote_request(1, NULL) == ESP_OK); run_gallery_once();
    assert(shows == before && !runtime_remote_operation_pending());
    assert(gallery_sleep_begin()); runtime_sleep_end();
    puts("OK actual gallery: unchanged image/strip, file update, UI recovery, retained ink, retry and remote no-op completion");
    return 0;
}
