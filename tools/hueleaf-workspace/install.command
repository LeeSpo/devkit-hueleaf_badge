#!/usr/bin/env bash
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
bash "$HERE/hueleaf.sh" "$@"
rc=$?
if [ "$rc" -ne 0 ]; then read -r -p '启动失败，按回车关闭…' _; fi
exit "$rc"
