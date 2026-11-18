#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_trash_seconddisk.sh /path/to/linuxbeectl
# docs/spec/delete-files.md, FD-TAKEN on a disk that is not the data folder's. The work is done by
# tools/second-disk-trash.sh, which says SKIPPED and passes where it cannot make such a disk.
exec sh "$(dirname "$0")/../../tools/second-disk-trash.sh" "$1"
