#!/bin/sh
# LOCKED ACCEPTANCE TEST. Written by the reviewer, not the builder.
# Usage: cli_trash_seconddisk.sh /path/to/linuxbeectl
# docs/spec/delete-files.md, FD-TAKEN on a disk that is not the data folder's. The work is done by
# tools/second-disk-trash.sh. Where it cannot make such a disk, or could not try one of its probes, it
# says SKIPPED and answers 77, which the gate shows as a test skipped, not passed.
exec sh "$(dirname "$0")/../../tools/second-disk-trash.sh" "$1"
