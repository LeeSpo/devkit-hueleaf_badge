#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "file_store.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int renames, unlinks, stats;
static int fail_rename, fail_rename_also, fail_unlink, fail_stat;

int test_rename(const char *from, const char *to)
{
    if (++renames == fail_rename || renames == fail_rename_also) {
        errno = EIO;
        return -1;
    }
    /* Model FAT's refusal to overwrite. */
    struct stat st;
    if (stat(to, &st) == 0) {
        errno = EEXIST;
        return -1;
    }
    return rename(from, to);
}

int test_unlink(const char *path)
{
    if (++unlinks == fail_unlink) {
        errno = EIO;
        return -1;
    }
    return unlink(path);
}

int test_stat(const char *path, struct stat *st)
{
    if (++stats == fail_stat) {
        errno = EIO;
        return -1;
    }
    return stat(path, st);
}

static void reset(void)
{
    renames = unlinks = stats = 0;
    fail_rename = fail_rename_also = fail_unlink = fail_stat = 0;
}

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text));
    assert(fclose(f) == 0);
}

static void expect(const char *path, const char *text)
{
    char buf[64] = {0};
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    assert(!ferror(f));
    assert(fclose(f) == 0);
    assert(n == strlen(text) && strcmp(buf, text) == 0);
}

static void absent(const char *path)
{
    struct stat st;
    assert(stat(path, &st) == -1 && errno == ENOENT);
}

static void clear(void)
{
    (void)unlink("target");
    (void)unlink("target.bak");
    (void)unlink("tmp");
    reset();
}

int main(void)
{
    char dir[] = "/tmp/file-store-test-XXXXXX";
    assert(mkdtemp(dir) && chdir(dir) == 0);

    assert(file_store_recover("target"));
    put("target.bak", "old");
    assert(file_store_recover("target"));
    expect("target", "old"); absent("target.bak");
    put("target.bak", "old"); put("target", "new");
    assert(file_store_recover("target"));
    expect("target", "new"); absent("target.bak");

    put("target.bak", "old"); put("target", "");
    assert(!file_store_recover("target"));
    expect("target", ""); expect("target.bak", "old");
    put("target", "new"); reset(); fail_unlink = 1;
    assert(!file_store_recover("target"));
    expect("target", "new"); expect("target.bak", "old");
    reset(); fail_stat = 2;
    assert(!file_store_recover("target"));
    expect("target", "new"); expect("target.bak", "old");
    clear(); put("target.bak", "old"); fail_rename = 1;
    assert(!file_store_recover("target"));
    absent("target"); expect("target.bak", "old");

    clear(); put("tmp", "new");
    assert(file_store_replace("tmp", "target"));
    expect("target", "new"); absent("tmp"); absent("target.bak");
    clear(); put("target", "old"); put("tmp", "new");
    assert(file_store_replace("tmp", "target"));
    expect("target", "new"); absent("target.bak");

    clear(); put("target", "old"); put("tmp", "new"); fail_rename = 1;
    assert(!file_store_replace("tmp", "target"));
    expect("target", "old"); expect("tmp", "new"); absent("target.bak");
    reset(); fail_rename = 2;
    assert(!file_store_replace("tmp", "target"));
    expect("target", "old"); expect("tmp", "new"); absent("target.bak");
    reset(); fail_rename = 2; fail_rename_also = 3;
    assert(!file_store_replace("tmp", "target"));
    absent("target"); expect("target.bak", "old"); expect("tmp", "new");
    reset(); assert(file_store_recover("target")); expect("target", "old");
    reset(); fail_unlink = 1;
    assert(file_store_replace("tmp", "target"));
    expect("target", "new"); expect("target.bak", "old");
    reset(); assert(file_store_recover("target")); absent("target.bak");

    clear(); put("target", "old"); put("tmp", "new"); fail_stat = 1;
    assert(!file_store_replace("tmp", "target"));
    expect("target", "old"); expect("tmp", "new");
    reset(); fail_stat = 4;
    assert(!file_store_replace("tmp", "target"));
    expect("target", "old"); expect("tmp", "new");
    reset(); assert(!file_store_replace("target", "target"));
    put("target.bak", "old");
    assert(!file_store_replace("target.bak", "target"));
    assert(!file_store_replace("missing", "target"));
    put("tmp", ""); assert(!file_store_replace("tmp", "target"));
    char long_path[192]; memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    assert(!file_store_recover(long_path));
    assert(!file_store_replace("tmp", long_path));
    expect("target", "old"); expect("target.bak", "old");
    clear();
    put("target.bak", "old");
    assert(file_store_recover_tree(".")); expect("target", "old"); absent("target.bak");
    put("target.bak", "old"); put("target", "new");
    assert(file_store_recover_tree(".")); expect("target", "new"); absent("target.bak");
    assert(file_store_recover_tree("does-not-exist"));
    assert(mkdir("img", 0700) == 0);
    put("img/photo.bin", "truncated"); put("img/photo.bin.bak", "old");
    assert(!file_store_recover_tree("img"));
    expect("img/photo.bin.bak", "old");
    assert(unlink("img/photo.bin") == 0);
    assert(file_store_recover_tree("img")); expect("img/photo.bin", "old");
    assert(unlink("img/photo.bin") == 0 && rmdir("img") == 0);
    clear(); assert(chdir("/") == 0); assert(rmdir(dir) == 0);
    return 0;
}
