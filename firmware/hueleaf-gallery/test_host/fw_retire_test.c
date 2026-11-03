#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "fw_retire.h"
static int ex(const char*p){return access(p,F_OK)==0;}
static void mk(const char*p){FILE*f=fopen(p,"w");fputs("x",f);fclose(f);}
int main(void){ int fails=0; char d[]="/tmp/fwrXXXXXX"; if(!mkdtemp(d))return 1; if(chdir(d))return 1;
 mk("firmware.bin"); mk("firmware.bin.done");                /* previous upgrade left .done */
 if(fw_retire("firmware.bin",".done")!=0||ex("firmware.bin")||!ex("firmware.bin.done")){puts("existing .done");fails++;}
 mk("firmware.bin"); unlink("firmware.bin.done"); mkdir("firmware.bin.done",0755); mk("firmware.bin.done/k"); /* .done undeletable */
 if(fw_retire("firmware.bin",".done")!=1||ex("firmware.bin")){puts("fallback delete");fails++;}
 if(fw_retire("firmware.bin",".done")!=1){puts("absent");fails++;}
 puts(fails?"FAIL":"OK fw_retire: stale .done replaced, undeletable .done -> firmware.bin deleted"); return fails; }
