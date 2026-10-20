// Run diagnostics for the iOS app (plan item 106; the ideas from lordran-native f5834c5, MIT, the code is ours). Checked by host/tests/t_rundiag.c
//   The interrupted-run marker: <dir>/boot.running exists while a session is in the foreground (main.m writes it on entering the foreground and
//   removes it on leaving it and at an orderly exit). A launch that finds it knows the last session ended in the foreground without either:
//   killed for memory, a crash, a hang the system ended, or a SIGKILL from a debugger. The marker names that session's log; rundiag_launch keeps
//   a copy of it as boot.interrupted.txt (a clone on APFS, so instant), replacing an older one, while the log itself rotates as before.
//   The report: a text file with the caller's header (device, Metal, memory, thermal) and the last bytes of each log (8 MiB in the app), each size
//   taken once at the start so a log still being written cannot make it longer (the share sheet's export, main.m).
#pragma once
#include <copyfile.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RUNDIAG_MARK "boot.running"
#define RUNDIAG_KEPT "boot.interrupted.txt"

// Marks this session as in the foreground, its log being <dir>/<log> (a file name; the log is renamed when the session comes to the foreground)
static inline int rundiag_mark(const char *dir, const char *log) {
    char p[1024], b[512]; snprintf(p, sizeof p, "%s/" RUNDIAG_MARK, dir);
    time_t t = time(NULL); struct tm u; gmtime_r(&t, &u); char ts[32]; strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", &u);
    int n = snprintf(b, sizeof b, "%s\t%s\tpid %d\n", log, ts, (int)getpid());
    int fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644); if (fd < 0) return -1;
    int ok = write(fd, b, (size_t)n) == n; close(fd); return ok ? 0 : -1;
}
static inline void rundiag_unmark(const char *dir) { char p[1024]; snprintf(p, sizeof p, "%s/" RUNDIAG_MARK, dir); unlink(p); }

// At launch, before this session's log is opened: 1 when the last session was interrupted (its log kept as boot.interrupted.txt; what = "<log>
// from <time> pid <n>"), 0 when it ended in order, -1 when the marker was there but its log could not be kept (what says why). The marker is gone
// afterwards either way
static inline int rundiag_launch(const char *dir, char *what, size_t n) {
    char p[1024], b[512] = {0}; snprintf(p, sizeof p, "%s/" RUNDIAG_MARK, dir);
    int fd = open(p, O_RDONLY); if (fd < 0) return 0;
    ssize_t r = read(fd, b, sizeof b - 1); close(fd); unlink(p);
    if (r < 0) r = 0;
    b[r] = 0; char *nl = strchr(b, '\n'); if (nl) *nl = 0;
    char *tab = strchr(b, '\t'); if (tab) *tab++ = 0;
    if (!*b || strchr(b, '/') || !strcmp(b, "..")) { snprintf(what, n, "the marker names no log file of this folder (\"%s\")", b); return -1; }
    snprintf(what, n, "%s from %s", b, tab ? tab : "?"); for (char *c = what; *c; c++) if (*c == '\t') *c = ' ';
    char src[1024], dst[1024]; snprintf(src, sizeof src, "%s/%s", dir, b); snprintf(dst, sizeof dst, "%s/" RUNDIAG_KEPT, dir);
#ifndef DM_TEST_RUNDIAG_NO_KEEP
    unlink(dst);
    if (copyfile(src, dst, NULL, COPYFILE_CLONE)) { size_t l = strlen(what); snprintf(what + l, n - l, " (its log could not be kept: %s)", strerror(errno)); return -1; }
#endif
    return 1;
}

// Appends "===== label =====" and the last limit bytes of path (as long as the file was when this started) to out; 0 on success
static inline int rundiag_tail(FILE *out, const char *path, const char *label, off_t limit) {
    fprintf(out, "\n===== %s =====\n", label);
    int fd = open(path, O_RDONLY | O_NOFOLLOW); struct stat st;
    if (fd < 0) { fprintf(out, "(none: %s)\n", strerror(errno)); return 0; }
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) { close(fd); fprintf(out, "(not a regular file)\n"); return 0; }
    off_t left = st.st_size, at = 0;
#ifndef DM_TEST_RUNDIAG_HEAD
    if (left > limit) { fprintf(out, "(the last %lld of %lld bytes)\n", (long long)limit, (long long)left); at = left - limit; left = limit; }
#else
    if (left > limit) left = limit;   // broken variant: the start of a long log, not its end
#endif
    char buf[65536];
    while (left > 0) {
        ssize_t r = pread(fd, buf, (size_t)(left < (off_t)sizeof buf ? left : (off_t)sizeof buf), at);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        if (fwrite(buf, 1, (size_t)r, out) != (size_t)r) { close(fd); return -1; }
        at += r; left -= r;
    }
    close(fd); return ferror(out) ? -1 : 0;
}

// The report at path: head, then the tail of each of the nlogs logs (names in dir); written to path.tmp and renamed, so a half-written report is
// never shared. 0 on success
static inline int rundiag_report(const char *path, const char *head, const char *dir, const char *const *logs, int nlogs, off_t limit) {
    char tmp[1100]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w"); if (!f) return -1;
    int bad = fputs(head, f) < 0;
    for (int i = 0; i < nlogs && !bad; i++) { char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, logs[i]); bad = rundiag_tail(f, p, logs[i], limit) != 0; }
    if (fclose(f)) bad = 1;
    if (bad || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}
