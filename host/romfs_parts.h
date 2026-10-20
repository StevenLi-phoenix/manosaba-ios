// RomFS parts unpacked (plan item 62e; tools/romfs_parts.py packs them, its docstring has the format). On the iOS app's launch (ios/Sources/main.m
// romfs_locate), when <src>/romfs.dmri is there: the parts still there are checked against the index (size, SHA-256) before anything is written;
// then part by part their files go into <dest>.partial and the part is deleted; when none is left, <dest>.partial becomes <dest> (an older <dest>
// is removed first) and the index goes. Killed in the middle, the next launch carries on with the parts left. A record whose path is absolute,
// empty, or has an empty, "." or ".." component is refused, as is a part whose records do not end at its end or count other than the index says.
#pragma once
#include <stddef.h>

// 1 unpacked (dest is complete), 0 a problem (why says; nothing deleted that is not unpacked), -1 no index in src. line: progress lines, or NULL
int dm_romfs_parts_unpack(const char *src, const char *dest, char *why, size_t why_n, void (*line)(const char *));
