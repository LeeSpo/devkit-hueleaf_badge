#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "platform.h"

static int failure, locks, fat_calls, replaces, uninstalls;
static bool ready;
static uint8_t output[9000];
static size_t output_len;
static FILE *mock_fopen(const char *path, const char *mode) { (void)path; (void)mode; return tmpfile(); }
static int mock_fflush(FILE *f) { int r = fflush(f); return failure == 1 ? EOF : r; }
static int mock_fsync(int fd) { (void)fd; return failure == 2 ? -1 : 0; }
static int mock_fclose(FILE *f) { int r = fclose(f); return failure == 3 ? EOF : r; }
static int mock_unlink(const char *p) { (void)p; return 0; }
static int mock_mkdir(const char *p, mode_t m) { (void)p; (void)m; return 0; }
#define fopen mock_fopen
#define fflush mock_fflush
#define fsync mock_fsync
#define fclose mock_fclose
#define unlink mock_unlink
#define mkdir mock_mkdir
#include PROTO_SOURCE
#undef fopen
#undef fflush
#undef fsync
#undef fclose
#undef unlink
#undef mkdir

int usb_serial_jtag_write_bytes(const void *p, size_t n, int t) { (void)t; assert(output_len+n <= sizeof(output)); memcpy(output+output_len,p,n); output_len+=n; return (int)n; }
int usb_serial_jtag_read_bytes(void *p,size_t n,int t) { (void)p;(void)n;(void)t; return 0; }
esp_err_t usb_serial_jtag_driver_install(const usb_serial_jtag_driver_config_t *c) { (void)c; return ESP_OK; }
esp_err_t usb_serial_jtag_driver_uninstall(void) { uninstalls++; return ESP_OK; }
int xTaskCreate(void (*f)(void *),const char *n,int s,void *a,int p,void *h) { (void)f;(void)n;(void)s;(void)a;(void)p;(void)h; return 0; }
void vTaskDelay(int t) { (void)t; }
void esp_restart(void) { abort(); }
void boot_select_restart_to_stock(void) { abort(); }
esp_err_t esp_vfs_fat_info(const char *p,uint64_t *a,uint64_t *b) { (void)p;(void)a;(void)b; fat_calls++; return ESP_OK; }
const esp_partition_t *esp_ota_get_running_partition(void) { return NULL; }
esp_err_t axp2101_gauge_status(bool *a,bool *b,int *c) { (void)a;(void)b;(void)c; return ESP_OK; }
esp_err_t pcf8563_set_time(const pcf8563_time_t *t) { (void)t; return ESP_OK; }
bool tf_card_is_mounted(void) { return true; }
esp_err_t tf_card_check(void) { return ESP_OK; }
bool tf_card_has_fault(void) { return false; }
bool gallery_is_ready(void) { return ready; }
void sd_lock(void) { locks++; }
void sd_unlock(void) {}
void gallery_get_state(int *c,int *n,char *s,size_t z,bool *b) { (void)c;(void)n;(void)s;(void)z;(void)b; }
void gallery_list(void (*cb)(const char *,void *),void *p) { (void)cb;(void)p; }
void gallery_label_path(char *p,size_t n,const char *s) { snprintf(p,n,"/labels/%s",s); }
int gallery_rescan(void) { return 0; }
esp_err_t gallery_show_name(const char *n) { (void)n; return ESP_OK; }
void gallery_nav(int n) { (void)n; }
void gallery_set_strip(bool b) { (void)b; }
size_t logbuf_read(char *p,size_t n) { (void)p;(void)n; return 0; }
bool file_store_replace(const char *tmp,const char *dst) { assert(strstr(tmp,".upload.tmp")); assert(!strcmp(dst,GALLERY_DIR "/photo.bin")); replaces++; return true; }
bool file_store_recover(const char *p) { (void)p; return true; }

static int status(int cmd,const uint8_t *p,uint32_t n) {
    output_len=0; handle(cmd, 42, p, n);
    assert(output_len>=20 && !memcmp(output,"HLP1",4));
    assert(output[4]==(cmd|0x80) && output[5]==42);
    assert(output_len == 16u + get32(output+8));
    assert(get32(output+output_len-4)==proto_crc32(0,output+4,output_len-8));
    return (int32_t)get32(output+12);
}
static void begin(void) {
    uint8_t p[14]={0}, zeros[SCREEN_FRAME_BYTES]={0};
    put32(p+1,SCREEN_FRAME_BYTES); put32(p+5,proto_crc32(0,zeros,sizeof(zeros))); memcpy(p+9,"photo",5);
    assert(status(CMD_UP_BEGIN,p,sizeof(p))==0);
}
static void complete(void) {
    uint8_t p[4096]={0};
    while (s_up_off < s_up_size) {
        uint32_t n = s_up_size - s_up_off;
        if (n > sizeof(p)-4) n = sizeof(p)-4;
        put32(p,s_up_off);
        assert(status(CMD_UP_DATA,p,n+4)==0);
    }
    assert(status(CMD_UP_END,NULL,0)==(failure ? -7 : 0));
}
int main(void) {
    uint8_t p[16]={0};
    assert(status(CMD_LIST,NULL,0)==-8);
    assert(status(CMD_UP_BEGIN,p,10)==-8);
    assert(status(CMD_INFO,NULL,0)==0 && locks==0 && fat_calls==0);
    ready=true; begin();
    FILE *old=s_up; uint32_t oldsize=s_up_size, oldcrc=s_up_crc; char final[sizeof(s_up_final)]; strcpy(final,s_up_final);
    assert(status(CMD_UP_BEGIN,p,10)==-5);
    assert(s_up==old && s_up_size==oldsize && s_up_crc==oldcrc && !strcmp(s_up_final,final));
    s_up_off=UINT32_MAX-1; s_up_size=UINT32_MAX; put32(p,s_up_off);
    assert(status(CMD_UP_DATA,p,8)==-5 && s_up_off==UINT32_MAX-1);
    s_up_size=oldsize; s_up_off=0; complete(); assert(replaces==1);
    for (failure=1; failure<=3; failure++) { begin(); complete(); assert(replaces==1); }
    assert(proto_start()==ESP_ERR_NO_MEM && uninstalls==1);
    return 0;
}
