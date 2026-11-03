#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "screen_driver_priv.h"
#include "screen_driver.h"
static uint8_t last_cmd; static uint8_t got[172800]; static size_t gotn; static int busyflip;
esp_err_t sd_write_cmd(uint8_t c){last_cmd=c; if(c==0x10) gotn=0; return 0;}
esp_err_t sd_write_data(const uint8_t*d,size_t n){ if(last_cmd==0x10){memcpy(got+gotn,d,n);gotn+=n;} return 0;}
esp_err_t sd_write_data_byte(uint8_t d){return sd_write_data(&d,1);}
esp_err_t sd_wait_busy(void){return 0;}
int sd_busy_level(void){return (busyflip++)&1;}
/* reference: FrameFilm encoder with m_mono_prev on a 1bpp bitmap */
static uint8_t rb(uint8_t x){x=(x>>4)|(x<<4);x=((x>>2)&0x33)|((x<<2)&0xCC);return((x>>1)&0x55)|((x<<1)&0xAA);}
static uint8_t prev[43200], ref[172800];
static void ref_write(const uint8_t*bm){ size_t k=0; for(int y=0;y<480;y++)for(int x=0;x<90;x++){uint8_t old=prev[y*90+x],nx=(uint8_t)~rb(bm?bm[y*90+x]:0);prev[y*90+x]=nx;for(int j=0;j<4;j++){ref[k++]=((old<<5)&0x20)|((nx<<4)&0x10)|(old&2)|((nx>>1)&1);old>>=2;nx>>=2;}}}
int main(void){ static uint8_t fr[172800], bm[43200]; srand(1); int fails=0;
 for(int it=0;it<45;it++){
  for(size_t i=0;i<sizeof(fr);i++){ uint8_t h=rand()%3?1:0,l=rand()%3?1:0; fr[i]=(fr[i]&0x88)|(h<<4)|l; }
  for(int y=0;y<480;y++)for(int x=0;x<90;x++){uint8_t b=0;for(int i=0;i<4;i++){uint8_t v=fr[y*360+4*x+i]; if(!((v>>4)&7)) b|=0x80>>(2*i); if(!(v&7)) b|=0x40>>(2*i);} bm[y*90+x]=b;}
  if(it==0) memset(prev,0x00,sizeof prev);
  if(it>0 && it%20==0){ ref_write(NULL); }  /* mono_clear */
  ref_write(bm);
  mono_fast_show_locked(fr);
  if(gotn!=172800||memcmp(got,ref,172800)){printf("mismatch it=%d\n",it);fails++;}
 } printf(fails?"FAIL\n":"OK 45 frames match FrameFilm encoder (incl. full clear + 2 mono_clear)\n"); return fails; }
