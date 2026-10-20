// Checks that a copied RomFS is complete and of the expected version (romfs_validate.c).
#ifndef DM_ROMFS_VALIDATE_H
#define DM_ROMFS_VALIDATE_H
#include <stddef.h>
// dir = the RomFS root (Data/ under it). Returns 1 when complete; why gets the reason it is not, or a one-line summary when it is
int dm_romfs_validate(const char *dir, char *why, size_t why_n);
// The same with a marker (plan item 175, after super-mario-odyssey-ios ea19678, MIT): the key files are checked every time; the tree is walked once
// and then trusted while marker (a file the caller may write) records this game, version, expected totals and the tree directory as it was then
// (device, inode, modification time). DM_ROMFS_RECHECK=1 walks it again. walked: 1 when this call walked the tree (may be NULL)
int dm_romfs_validate_marked(const char *dir, const char *marker, char *why, size_t why_n, int *walked);
// Full report for the user: key files present / total and the Data totals; returns the number of problems (0 = complete)
int dm_romfs_report(const char *dir, char *out, size_t out_n);
#endif
