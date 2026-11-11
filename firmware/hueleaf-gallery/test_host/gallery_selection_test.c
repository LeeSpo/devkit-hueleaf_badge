#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
/* Real filesystem + replacement transaction, while the display, NVS and card
 * inventory are the existing gallery harness. No real /sdcard access. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../main/file_store.c"
static char root[]="/tmp/hl-selection-XXXXXX", selection_path[256], temp_path[256];
static int io_failure;
static char *map_selection(const char *p)
{ return strstr(p,".slideshow.tmp") ? temp_path : selection_path; }
static FILE *selection_open(const char *p,const char *m) { return fopen(map_selection(p),m); }
static int selection_unlink(const char *p) { return unlink(map_selection(p)); }
static int selection_sync(int fd) { return io_failure==1 ? -1 : fsync(fd); }
static int selection_flush(FILE *f) { int r=fflush(f); return io_failure==2 ? EOF : r; }
static int selection_puts(const char *s,FILE *f) { return io_failure==3 ? EOF : fputs(s,f); }
static int selection_close(FILE *f) { int r=fclose(f); return io_failure==4 ? EOF : r; }
static int selection_fprintf(FILE *f,const char *fmt,const char *s)
{ return io_failure==3 ? -1 : fprintf(f,fmt,s); }
static bool selection_replace(const char *from,const char *to)
{ (void)from;(void)to; return io_failure==5 ? false : file_store_replace(temp_path,selection_path); }
static bool selection_recover(const char *path)
{ (void)path; return file_store_recover(selection_path); }
/* Reuse device stubs but include the production gallery with real selection I/O. */
#define fsync selection_sync
#define fflush selection_flush
#define fclose selection_close
#define fputs selection_puts
#define fprintf selection_fprintf
#define unlink selection_unlink
#define file_store_replace selection_replace
#define file_store_recover selection_recover
#define GALLERY_SELECTION_IO 1
#define GALLERY_NO_WORKER_HELPERS
#undef GALLERY_SOURCE
#define GALLERY_SOURCE "../main/gallery.c"
#include "gallery_fixture.h"
#undef fsync
#undef fflush
#undef fclose
#undef fputs
#undef fprintf
#undef unlink
#undef file_store_replace
#undef file_store_recover

static uint64_t token(uint8_t *out)
{ uint64_t v=0; for(int i=0;i<8;i++)v|=(uint64_t)out[i]<<(i*8); return v; }
static void write_list(const char *s)
{ FILE *f=fopen(selection_path,"wb"); assert(f); assert(fputs(s,f)>=0); assert(!fclose(f)); }
static void check_file(const char *s)
{ char buf[256]; FILE *f=fopen(selection_path,"rb"); assert(f);size_t n=fread(buf,1,sizeof(buf)-1,f);buf[n]=0;fclose(f);assert(!strcmp(buf,s)); }
int main(void)
{
    assert(mkdtemp(root));
    snprintf(selection_path,sizeof(selection_path),"%s/slideshow.txt",root);
    snprintf(temp_path,sizeof(temp_path),"%s/.slideshow.tmp",root);
    assert(gallery_prepare()==ESP_OK);
    s_n=4;s_cur=0;
    for(int i=0;i<4;i++)snprintf(s_names[i],GALLERY_NAME_MAX,"%c.bin",'a'+i);
    selection_load();publish_state();
    assert(!s_selection_custom && !s_selection_fault);
    uint8_t page[8192],bits[GALLERY_SELECTION_BYTES]={5};size_t used;
    assert(gallery_selection_read(0,0,page,sizeof(page),&used)==ESP_OK);
    uint64_t v=token(page);
    int activity_before=activity_count;
    assert(gallery_selection_save(v,4,bits,1)==ESP_OK);
    assert(activity_count==activity_before+1);
    io_failure=5; /* a lost-reply retry does not write again or reject old token */
    assert(gallery_selection_save(v,4,bits,1)==ESP_OK);
    io_failure=0;
    check_file("HLSEL1\na.bin\nc.bin\n");
    assert(s_selected[0]&&!s_selected[1]&&s_selected[2]&&!s_selected[3]);
    memset(s_selected,1,sizeof(s_selected));selection_load();assert(s_selected[0]&&!s_selected[1]);
    assert(s_selection_custom);
    char backup[280];snprintf(backup,sizeof(backup),"%s.bak",selection_path);
    assert(!rename(selection_path,backup));selection_load();
    assert(s_selected[0]&&!s_selected[1]&&!s_selection_fault); /* rollback backup never becomes legacy all */
    /* Failed flush/write/sync/close/replace never changes either source. */
    for(io_failure=1;io_failure<=5;io_failure++) {
        assert(gallery_selection_read(0,0,page,sizeof(page),&used)==ESP_OK);v=token(page);
        bits[0]=2;assert(gallery_selection_save(v,4,bits,1)==ESP_FAIL);
        assert(s_selected[0]&&!s_selected[1]&&s_selected[2]);
        check_file("HLSEL1\na.bin\nc.bin\n");
    }
    io_failure=0;
    assert(gallery_selection_read(0,0,page,sizeof(page),&used)==ESP_OK);v=token(page);
    bits[0]=0;assert(gallery_selection_save(v,4,bits,1)==ESP_OK);
    check_file("HLSEL1\n");selection_load();assert(s_selection_custom&&!s_selected[0]);
    /* New upload is excluded, same-name overwrite and reordering are stable. */
    s_n=5;strcpy(s_names[4],"new.bin");selection_load();assert(!s_selected[4]);
    write_list("HLSEL1\na.bin\nc.bin\n");selection_load();
    strcpy(s_names[0],"d.bin");strcpy(s_names[3],"a.bin");selection_load();
    assert(!s_selected[0]&&s_selected[2]&&s_selected[3]);
    strcpy(s_names[2],"deleted-c.bin");selection_load();assert(!s_selected[2]&&s_selected[3]);
    /* Old inventory token must reject stale paging and saves before writing. */
    assert(gallery_selection_read(0,0,page,sizeof(page),&used)==ESP_OK);v=token(page);
    strcpy(s_names[1],"changed.bin");
    assert(gallery_selection_read(1,v,page,sizeof(page),&used)==ESP_ERR_INVALID_STATE);
    assert(gallery_selection_save(v,5,bits,1)==ESP_ERR_INVALID_STATE);
    assert(gallery_selection_read(0,0,page,sizeof(page),&used)==ESP_OK);v=token(page);
    bits[0]=0x80;assert(gallery_selection_save(v,5,bits,1)==ESP_ERR_INVALID_ARG);
    const char *bad[]={"", "broken\n", "HLSEL1\n../a.bin\n", "HLSEL1\na.bin", "HLSEL1\n\n"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        write_list(bad[i]);selection_load();assert(s_selection_fault&&auto_target(0)<0);
    }
    FILE *binary=fopen(selection_path,"wb");assert(binary);
    const char nul_line[]="HLSEL1\na.bin\0extra\n";
    assert(fwrite(nul_line,1,sizeof(nul_line)-1,binary)==sizeof(nul_line)-1);assert(!fclose(binary));
    selection_load();assert(s_selection_fault);
    /* Malformed list can be repaired by an explicit save. */
    assert(gallery_selection_read(0,0,page,sizeof(page),&used)==ESP_OK && (page[14]&2));
    bits[0]=0;assert(gallery_selection_save(token(page),5,bits,1)==ESP_OK&&!s_selection_fault);
    /* Full capacity: long filenames exceed a single protocol payload; 64-record
     * pages carry everything; one tiny bitset commits all 300 names atomically. */
    s_n=GALLERY_MAX;s_selection_fault=false;s_selection_custom=false;
    for(int i=0;i<s_n;i++) {
        snprintf(s_names[i],GALLERY_NAME_MAX,"%054d.bin",i);
        s_selected[i]=true;
    }
    int start=0,pages=0;v=0;
    do {
        assert(gallery_selection_read(start,v,page,sizeof(page),&used)==ESP_OK && used<=8192);
        v=token(page);int count=page[12]|page[13]<<8;
        assert(count>0&&count<=64&&(page[8]|page[9]<<8)==GALLERY_MAX);
        size_t off=15;
        for(int i=0;i<count;i++) {
            assert(page[off]==1);size_t n=page[off+1];off+=2;
            assert(n==strlen(s_names[start+i])&&!memcmp(page+off,s_names[start+i],n));off+=n;
        }
        assert(off==used);start+=count;pages++;
    } while(start<s_n);
    assert(pages==5);
    memset(bits,0xff,sizeof(bits));bits[sizeof(bits)-1]=0x0f;
    assert(gallery_selection_save(v,GALLERY_MAX,bits,sizeof(bits))==ESP_OK);
    memset(s_selected,0,sizeof(s_selected));selection_load();
    for(int i=0;i<s_n;i++)assert(s_selected[i]);
    s_n=0;
    assert(gallery_selection_read(0,0,page,sizeof(page),&used)==ESP_OK);
    assert(gallery_selection_save(token(page),0,bits,0)==ESP_OK);
    check_file("HLSEL1\n");
    assert(!unlink(selection_path));assert(!rmdir(root));
    puts("OK gallery selection: real atomic persistence, faults, stale snapshots, 300 images");
    return 0;
}
