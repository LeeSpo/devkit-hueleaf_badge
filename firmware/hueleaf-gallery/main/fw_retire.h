#pragma once
/* Get an applied/failed TF update file out of the way without relying on
 * FAT rename-over-existing (which fails on FAT):
 *   1. unlink(path+suffix) if it exists, 2. rename(path, path+suffix),
 *   3. else unlink(path).
 * Returns 0 renamed, 1 deleted, -1 file still there (caller must remember
 * the image in NVS so it is never re-applied). Pure POSIX, host-tested. */
int fw_retire(const char *path, const char *suffix);
