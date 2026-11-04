#include "file_store.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define STORE_PATH_SIZE 192

static bool backup_path(const char *path, char backup[STORE_PATH_SIZE])
{
    if (!path || !*path || strlen(path) >= STORE_PATH_SIZE - sizeof(".bak") + 1)
        return false;
    return snprintf(backup, STORE_PATH_SIZE, "%s.bak", path) > 0;
}

/* Distinguish absence from an inaccessible or otherwise uninspectable file. */
static bool inspect(const char *path, bool *exists, struct stat *st)
{
    if (stat(path, st) == 0) {
        *exists = true;
        return true;
    }
    if (errno == ENOENT) {
        *exists = false;
        return true;
    }
    return false;
}

bool file_store_recover(const char *path)
{
    char backup[STORE_PATH_SIZE];
    struct stat target_stat, backup_stat;
    bool target_exists, backup_exists;

    if (!backup_path(path, backup) ||
        !inspect(path, &target_exists, &target_stat) ||
        !inspect(backup, &backup_exists, &backup_stat))
        return false;
    if (!backup_exists)
        return true;
    if (!target_exists)
        return rename(backup, path) == 0;
    if (!S_ISREG(target_stat.st_mode) || target_stat.st_size <= 0)
        return false;
    /* Image files have a fixed on-card size. Do not discard a known-good
     * backup for a truncated replacement after a filesystem/power failure. */
    size_t len = strlen(path);
    if ((strstr(path, "/img/") || !strncmp(path, "img/", 4)) && len >= 4 &&
        !strcasecmp(path + len - 4, ".bin") && target_stat.st_size != 172800)
        return false;
    return unlink(backup) == 0;
}

bool file_store_recover_tree(const char *directory)
{
    DIR *dir = opendir(directory);
    if (!dir) return errno == ENOENT;
    bool ok = true;
    struct dirent *entry;
    for (;;) {
        errno = 0;
        entry = readdir(dir);
        if (!entry) {
            if (errno) ok = false;
            break;
        }
        size_t n = strlen(entry->d_name);
        if (n <= 4 || strcasecmp(entry->d_name + n - 4, ".bak")) continue;
        char path[STORE_PATH_SIZE];
        int len = snprintf(path, sizeof(path), "%s/%.*s", directory,
                           (int)(n - 4), entry->d_name);
        if (len < 0 || len >= (int)sizeof(path) || !file_store_recover(path)) {
            ok = false;
            break;
        }
        /* Restart after mutation: FAT directory enumeration is not a snapshot. */
        rewinddir(dir);
    }
    if (closedir(dir) != 0) ok = false;
    return ok;
}

bool file_store_replace(const char *tmp, const char *path)
{
    char backup[STORE_PATH_SIZE];
    struct stat st;
    bool target_exists, tmp_exists;

    if (!tmp || !*tmp || !backup_path(path, backup) ||
        strcmp(tmp, path) == 0 || strcmp(tmp, backup) == 0 ||
        !inspect(tmp, &tmp_exists, &st) || !tmp_exists ||
        !S_ISREG(st.st_mode) || st.st_size <= 0)
        return false;
    if (!file_store_recover(path) || !inspect(path, &target_exists, &st))
        return false;
    if (target_exists && rename(path, backup) != 0)
        return false;
    if (rename(tmp, path) != 0) {
        if (target_exists)
            (void)rename(backup, path);
        return false;
    }
    /* Commit is complete. Cleanup failure must not make callers skip the
     * rescan; the backup remains for recovery at the next startup. */
    if (target_exists) (void)unlink(backup);
    return true;
}
