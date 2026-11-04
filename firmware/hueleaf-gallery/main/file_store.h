#ifndef FILE_STORE_H
#define FILE_STORE_H

#include <stdbool.h>

/* Recover an interrupted replacement, or report an error without discarding
 * the backup. A target alongside a backup must be nonempty to be committed. */
bool file_store_recover(const char *path);
/* Recover .bak files in one known storage directory before scanning it. */
bool file_store_recover_tree(const char *directory);

/* tmp must be a complete, closed file, distinct from path and path.bak,
 * including filesystem aliases (e.g. FAT case variants). Calls must be
 * serialized and paths must not be modified concurrently. A false result can
 * leave a backup for a later recovery. A completed rename returns true even if
 * backup cleanup is deferred. FAT/hardware failures can prevent rollback;
 * no filesystem atomicity or corruption recovery is promised. */
bool file_store_replace(const char *tmp, const char *path);

#endif
