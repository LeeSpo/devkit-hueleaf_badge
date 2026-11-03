#include "fw_retire.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int fw_retire(const char *path, const char *suffix)
{
    char dst[128];
    if (snprintf(dst, sizeof dst, "%s%s", path, suffix) >= (int)sizeof dst) return unlink(path) == 0 ? 1 : -1;
    (void)unlink(dst);
    if (rename(path, dst) == 0) return 0;
    if (unlink(path) == 0) return 1;
    return access(path, F_OK) == 0 ? -1 : 1;
}
