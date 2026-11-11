#include "gallery_fixture.h"

static void test_button_navigation(void)
{
    /* Left/BOOT goes back; right/PEK goes forward, wrapping at both ends. */
    assert(s_n==3 && s_cur==0);
    int before=shows;
    assert(gallery_btn_boot_short()); run_gallery_once(); assert(s_cur==2);
    assert(gallery_btn_pek_short()); run_gallery_once(); assert(s_cur==0);
    assert(gallery_btn_pek_short()); run_gallery_once(); assert(s_cur==1);
    assert(gallery_btn_boot_short()); run_gallery_once(); assert(s_cur==0);
    assert(shows==before+4);
    assert(gallery_tap_next()); run_gallery_once(); assert(s_cur==1);

    /* Refreshing and queued input must not accumulate extra navigation. */
    s_rendering=true;
    int old=notifications;
    before=shows;
    assert(!gallery_btn_boot_short() && !gallery_btn_pek_short() && !gallery_tap_next());
    assert(s_pending==0 && notifications==old && shows==before);
    s_rendering=false;
    assert(gallery_btn_boot_short());
    old=notifications;
    assert(!gallery_btn_boot_short() && !gallery_btn_pek_short() && !gallery_tap_next());
    assert(notifications==old && shows==before);
    run_gallery_once(); assert(s_cur==0 && shows==before+1);
}

static void test_picker_controls(void)
{
    /* Exercise both picker modes across a page boundary without drawing it. */
    int count=s_n, before=shows;
    s_n=PK_ROWS+2;
    assert(gallery_btn_boot_long() && pk_on && pk_cur==s_cur);
    assert(gallery_btn_boot_short() && pk_cur==1);
    assert(gallery_btn_pek_short() && pk_cur==PK_ROWS);
    assert(gallery_btn_pek_short() && pk_cur==0);
    tap_on=true;
    assert(gallery_btn_pek_short() && pk_cur==s_n-1);
    assert(gallery_btn_boot_short() && pk_cur==0);
    assert(gallery_tap_next() && pk_cur==PK_ROWS);
    tap_on=false;
    s_n=count; pk_cur=s_cur;
    assert(gallery_btn_boot_long() && pk_confirm);
    assert(picker_step()==portMAX_DELAY && !pk_on && !s_force && shows==before);

    /* Long-press selection still displays the highlighted image. */
    assert(gallery_btn_boot_long() && pk_on);
    assert(gallery_btn_boot_short() && pk_cur==1);
    assert(gallery_btn_boot_long() && pk_confirm);
    assert(picker_step()==portMAX_DELAY && !pk_on);
    run_gallery_once(); assert(s_cur==1 && shows==before+1);
}

int main(void) {
    fail_alloc=1; assert(gallery_prepare()==ESP_ERR_NO_MEM); assert(!s_sd && !s_state);
    fail_alloc=alloc_calls+2; int old_deletes=deletes;
    assert(gallery_prepare()==ESP_ERR_NO_MEM); assert(deletes==old_deletes+1); assert(!s_sd && !s_state);
    fail_alloc=0; assert(gallery_prepare()==ESP_OK); void *sd=s_sd, *state=s_state; int calls=alloc_calls;
    assert(gallery_prepare()==ESP_OK && s_sd==sd && s_state==state && alloc_calls==calls);
    assert(!gallery_is_ready()); gallery_set_ready(true); assert(gallery_is_ready()); gallery_set_ready(false); assert(!gallery_is_ready());
    s_n=0; s_cur=-1; assert(gallery_render_initial()==ESP_OK && shows==1);
    display_result=ESP_FAIL; assert(gallery_render_initial()==ESP_FAIL && shows==2); display_result=ESP_OK;
    s_n=1; s_cur=0; strcpy(s_names[0],"one.bin"); s_strip=false;
    assert(gallery_render_initial()==ESP_OK && shows==3); assert(shown_byte()==0x55);
    task_result=0; assert(gallery_start_task()==ESP_ERR_NO_MEM); task_result=pdPASS; assert(gallery_start_task()==ESP_OK);
    s_n=0; s_cur=-1; int before=shows; if (!setjmp(escape)) gallery_task(NULL); assert(waits==1 && shows==before);
    int old=notifications;
    s_force=false; gallery_rescan(false);
    assert(notifications==old && !s_force && shows==before);
    gallery_rescan(true); assert(notifications==old+1 && s_force);
    /* Quiet scanning preserves the current file even when new names sort
     * before it, without drawing or changing the persisted selection. */
    card_names[0]="one.bin"; card_names[1]="z-last.bin"; card_count=2;
    s_n=1; s_cur=0; strcpy(s_names[0],"one.bin"); strcpy(saved_name,"one.bin");
    s_force=false; old=notifications; before=shows;
    assert(gallery_rescan(false)==2);
    card_names[2]="a-first.bin"; card_count=3;
    assert(gallery_rescan(false)==3 && s_cur==1 && !strcmp(s_names[s_cur],"one.bin"));
    assert(notifications==old && shows==before && !s_force && !strcmp(saved_name,"one.bin"));
    assert(gallery_init()==ESP_OK && s_cur==1 && !strcmp(s_names[s_cur],"one.bin"));
    assert(gallery_render_initial()==ESP_OK);
    assert(shown_byte()==0x55);
    /* An overwrite stays invisible until explicitly drawn/restarted. */
    frame_byte=0x66; old=notifications; before=shows;
    gallery_rescan(false); assert(notifications==old && shows==before);
    assert(gallery_init()==ESP_OK && s_cur==1);
    assert(gallery_render_initial()==ESP_OK);
    assert(shown_byte()==0x66);
    /* Starting with an empty card: uploads leave the empty message intact,
     * and startup later selects the first image. */
    card_count=0; saved_name[0]=0; assert(gallery_init()==ESP_OK && s_n==0);
    assert(gallery_render_initial()==ESP_OK); old=notifications; before=shows;
    card_count=3; gallery_rescan(false);
    assert(notifications==old && shows==before && !s_force);
    assert(gallery_init()==ESP_OK && s_cur==0 && !strcmp(s_names[0],"a-first.bin"));
    assert(gallery_render_initial()==ESP_OK);
    gallery_set_ready(true);
    test_button_navigation();
    test_picker_controls();
    /* Session admission and physical input share the same state lock. */
    old=notifications; before=shows;
    assert(gallery_upload_session_begin());
    assert(!gallery_btn_boot_short() && !gallery_btn_pek_short());
    assert(!gallery_btn_boot_long() && !gallery_tap_next());
    gallery_nav(1); gallery_set_strip(true);
    assert(gallery_show_name("one.bin")==ESP_ERR_INVALID_STATE);
    assert(s_pending==0 && !s_request[0] && !s_force && !pk_on);
    assert(notifications==old && shows==before);
    assert(runtime_session_end());
    assert(notifications==old && shows==before);
    assert(gallery_btn_boot_short() && s_pending==-1);
    assert(!gallery_upload_session_begin());
    s_pending=0;
    assert(gallery_upload_file_begin());
    old=notifications;
    assert(!gallery_btn_boot_long() && !gallery_btn_boot_short() && !gallery_btn_pek_short() && !gallery_tap_next());
    assert(s_pending==0 && notifications==old);
    runtime_file_end();
    assert(gallery_btn_boot_long() && pk_on);
    assert(!gallery_upload_session_begin());
    pk_on=false; pk_drawn=false; pk_dirty=false; s_force=false;
    s_cur=-1; before=shows;
    assert(gallery_btn_pek_short()); run_gallery_once();
    assert(s_cur==0 && shows==before+1);  /* First next after a formerly empty album. */
    before=shows;
    assert(gallery_render_scheduled(s_names[0]) == ESP_OK && s_cur == 1 && shows == before + 1);
    assert(gallery_render_scheduled(s_names[2]) == ESP_OK && s_cur == 0 && shows == before + 2);
    before=shows;
    int ok_before=frame_ok;
    char persisted_before[64]; strcpy(persisted_before, saved_name);
    uint64_t retained_content;
    assert(file_fingerprint(s_names[2], &retained_content));
    view_saved(2, s_n, s_names[2], retained_content);
    gallery_boot_init(true);
    assert(gallery_restore_retained(s_names[2]) == ESP_OK && s_cur == 2 && shows == before);
    assert(frame_ok == ok_before + 1 && !s_force && !strcmp(saved_name, persisted_before));
    assert(gallery_restore_retained("missing.bin") == ESP_OK && shows == before + 1);
    int old_cur = s_cur;
    display_result=ESP_FAIL;
    assert(gallery_render_scheduled(s_names[0]) == ESP_FAIL && s_cur == old_cur);
    display_result=ESP_OK;
    s_cur=0;
    int activity_before=activity_count;
    slide_due=false; assert(!gallery_auto_next()); slide_due=true;
    assert(gallery_auto_next() && !gallery_sleep_begin() && activity_count == activity_before);
    run_gallery_once(); assert(s_cur == 1);
    assert(gallery_sleep_begin());
    assert(!gallery_auto_next() && !gallery_btn_pek_short() && !gallery_upload_session_begin());
    runtime_sleep_end();
    s_n=1; s_cur=0; before=shows;
    assert(file_fingerprint(s_names[0], &retained_content));
    view_saved(0, s_n, s_names[0], retained_content);
    gallery_boot_init(true);
    assert(gallery_render_scheduled(s_names[0]) == ESP_OK && shows == before);
    assert(gallery_restore_retained(s_names[0]) == ESP_OK && shows == before);
    assert(!gallery_auto_next());
    s_n=0; s_cur=-1;
#ifdef CONFIG_HL_REMOTE
    assert(gallery_sleep_begin()); runtime_sleep_end();
#else
    assert(!gallery_sleep_begin());
#endif
    assert(frame_ok > 0 && frame_failed > 0);
    /* BLE admission reserves queued navigation before render task starts. */
    s_n=3; s_cur=0; s_force=false; s_pending=0; s_request[0]=0; s_view.magic=0;
    before=shows;
    assert(gallery_remote_request(+1, NULL) == ESP_OK);
    assert(runtime_remote_operation_pending());
    assert(gallery_remote_request(+1, NULL) == ESP_ERR_INVALID_STATE);
    assert(!gallery_btn_pek_short() && !gallery_sleep_begin() && !gallery_upload_session_begin());
    run_gallery_once();
    assert(s_cur == 1 && shows == before+1 && !runtime_remote_operation_pending());
    before=shows;
    gallery_restore_remote("removed.bin");
    assert(s_cur == 0 && !s_force && shows == before+1);
    assert(gallery_remote_request(0, "removed.bin") == ESP_ERR_NOT_FOUND);
    assert(!runtime_remote_operation_pending());
    assert(gallery_remote_request(0, s_names[0]) == ESP_OK);
    display_result=ESP_FAIL;
    run_gallery_once();
    assert(!runtime_remote_operation_pending() && s_cur == 0);
    display_result=ESP_OK;


    /* Timed navigation and timer boot use the same name-based filter.
     * Manual navigation still uses the complete gallery. */
    card_names[0]="a.bin"; card_names[1]="b.bin"; card_names[2]="c.bin"; card_names[3]="d.bin";
    card_count=4; selection_text="HLSEL1\na.bin\nc.bin\n";
    gallery_rescan(false); s_cur=0; s_force=false; before=shows;
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==2 && shows==before+1);
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==0);
    assert(gallery_btn_pek_short()); run_gallery_once(); assert(s_cur==1); /* B remains manual */
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==2);
    before=shows;
    assert(gallery_render_scheduled("b.bin")==ESP_OK && s_cur==2 && shows==before+1);
    assert(gallery_render_scheduled("c.bin")==ESP_OK && s_cur==0 && shows==before+2);
    assert(gallery_render_scheduled("missing.bin")==ESP_OK && s_cur==0);
    selection_text="HLSEL1\nc.bin\n"; gallery_rescan(false);
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==2);
    before=shows;
    assert(!gallery_auto_next());
    assert(gallery_render_scheduled("c.bin")==ESP_OK && shows==before);
    assert(file_fingerprint("b.bin", &retained_content));
    view_saved(1, s_n, "b.bin", retained_content);
    gallery_boot_init(true);
    assert(gallery_restore_retained("b.bin")==ESP_OK && s_cur==1 && shows==before);
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==2);
    selection_text="HLSEL1\n"; gallery_rescan(false); before=shows;
    assert(!gallery_auto_next());
    assert(file_fingerprint("b.bin", &retained_content));
    view_saved(1, s_n, "b.bin", retained_content);
    gallery_boot_init(true);
    assert(gallery_render_scheduled("b.bin")==ESP_OK && s_cur==1 && shows==before);
    assert(gallery_sleep_begin()); runtime_sleep_end();
    assert(gallery_btn_pek_short()); run_gallery_once(); assert(s_cur==2);
    selection_text="broken\n"; gallery_rescan(false); before=shows;
    runtime_snapshot_t snapshot; runtime_snapshot(&snapshot);
    assert(snapshot.slideshow_fault && !gallery_auto_next());
    assert(file_fingerprint("b.bin", &retained_content));
    view_saved(1, s_n, "b.bin", retained_content);
    gallery_boot_init(true);
    assert(gallery_render_scheduled("b.bin")==ESP_OK && shows==before);
    selection_text=NULL; gallery_rescan(false); s_cur=0;
    assert(gallery_auto_next()); run_gallery_once(); assert(s_cur==1); /* missing file = legacy all */
#ifdef CONFIG_HL_REMOTE
    /* All compact UI entrances must use MonoFast, including external frames. */
    static uint8_t external[FRAME_UI_BYTES];
    int mono_before=mono_calls;
    assert(gallery_show_ui_frame(external)==ESP_OK && last_mono_bitmap==external);
    assert(runtime_display_begin());
    render_picker(0,1,s_names);
    assert(last_mono_bitmap==s_frame && mono_calls==mono_before+2);
    display_result=ESP_FAIL;
    assert(gallery_show_ui_frame(external)==ESP_FAIL);
    runtime_snapshot(&snapshot); assert(!snapshot.display);
    display_result=ESP_OK;
#endif
    return 0;
}
