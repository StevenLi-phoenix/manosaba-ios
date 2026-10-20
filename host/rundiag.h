// Run diagnostics for the iOS app (plan item 106; the ideas from lordran-native f5834c5, MIT, the code is ours). Checked by host/tests/t_rundiag.c
//   The interrupted-run marker: <dir>/boot.running exists while a session is in the foreground (main.m writes it on entering the foreground and
//   removes it on leaving it and at an orderly exit). A launch that finds it knows the last session ended in the foreground without either:
//   killed for memory, a crash, a hang the system ended, or a SIGKILL from a debugger. The marker names that session's log; rundiag_launch keeps
//   a copy of it as boot.interrupted.txt (a clone on APFS, so instant), replacing an older one, while the log itself rotates as before.
//   The report: a text file with the caller's header (device, Metal, memory, thermal) and the last bytes of each log (8 MiB in the app), each size
//   taken once at the start so a log still being written cannot make it longer (the share sheet's export, main.m). The report is redacted line by
//   line before it is shared (plan item 178, lordran-native 40a9a72's rules, MIT, written anew in C): the app's container and bundle paths, home
//   folders' user names, UUIDs, MAC and IPv4 addresses, e-mail addresses.
#pragma once
#include <ctype.h>
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

// One line (n bytes, no newline) redacted into out (cap bytes); returns the length written. Placeholders: <app data> / <app bundle> for an app
// container (/var/mobile/Containers/Data/Application/<uuid>, /var/containers/Bundle/Application/<uuid>, with or without /private), /Users/<user>,
// <uuid>, <mac>, <ip>, <email>
static inline int rundiag_hex(const char *p, int n) { for (int i = 0; i < n; i++) if (!isxdigit((unsigned char)p[i])) return 0; return 1; }
static inline int rundiag_uuid(const char *p, size_t left) {   // 8-4-4-4-12 hex digits
    return left >= 36 && rundiag_hex(p, 8) && p[8] == '-' && rundiag_hex(p + 9, 4) && p[13] == '-' && rundiag_hex(p + 14, 4) && p[18] == '-' && rundiag_hex(p + 19, 4)
        && p[23] == '-' && rundiag_hex(p + 24, 12) && (left == 36 || !isxdigit((unsigned char)p[36]));
}
static inline size_t rundiag_redact_line(const char *in, size_t n, char *out, size_t cap) {
#ifndef DM_TEST_RUNDIAG_NO_REDACT
    static const struct { const char *pre, *ph; } cont[] = {{"/private/var/mobile/Containers/Data/Application/", "<app data>"}, {"/var/mobile/Containers/Data/Application/", "<app data>"},
        {"/private/var/containers/Bundle/Application/", "<app bundle>"}, {"/var/containers/Bundle/Application/", "<app bundle>"}};
#endif
    size_t o = 0, i = 0;
#define RD_PUT(str) do { size_t l_ = strlen(str); if (o + l_ < cap) { memcpy(out + o, str, l_); o += l_; } } while (0)
    while (i < n) {
        int done = 0;
#ifndef DM_TEST_RUNDIAG_NO_REDACT   // negative control of t_rundiag only: copied as it is
        for (size_t k = 0; k < sizeof cont / sizeof *cont && !done; k++) { size_t pl = strlen(cont[k].pre);
            if (n - i >= pl + 36 && !memcmp(in + i, cont[k].pre, pl) && rundiag_uuid(in + i + pl, n - i - pl)) { RD_PUT(cont[k].ph); i += pl + 36; done = 1; } }
        if (!done && n - i > 7 && !memcmp(in + i, "/Users/", 7) && (i == 0 || !isalnum((unsigned char)in[i - 1]))) {
            size_t j = i + 7; while (j < n && in[j] != '/' && in[j] != ' ' && in[j] != '"' && in[j] != '\'' && in[j] != ':') j++;
            if (j > i + 7 && strncmp(in + i + 7, "Shared", j - i - 7)) { RD_PUT("/Users/<user>"); i = j; done = 1; } }
        if (!done && (i == 0 || !isxdigit((unsigned char)in[i - 1])) && rundiag_uuid(in + i, n - i)) { RD_PUT("<uuid>"); i += 36; done = 1; }
        if (!done && n - i >= 17 && (i == 0 || !isxdigit((unsigned char)in[i - 1]))) {   // aa:bb:cc:dd:ee:ff
            int mac = 1; for (int g = 0; g < 6 && mac; g++) mac = rundiag_hex(in + i + 3 * g, 2) && (g == 5 || in[i + 3 * g + 2] == ':');
            if (mac && (n - i == 17 || !isxdigit((unsigned char)in[i + 17]))) { RD_PUT("<mac>"); i += 17; done = 1; } }
        if (!done && isdigit((unsigned char)in[i]) && (i == 0 || (!isalnum((unsigned char)in[i - 1]) && in[i - 1] != '.'))) {   // a.b.c.d, each 0-255
            size_t j = i; int parts = 0, ok = 1;
            while (parts < 4 && ok) { int v = 0, d = 0; while (j < n && isdigit((unsigned char)in[j]) && d < 4) { v = v * 10 + in[j++] - '0'; d++; }
                ok = d >= 1 && d <= 3 && v <= 255; parts++; if (parts < 4) { if (ok && j < n && in[j] == '.') j++; else ok = 0; } }
            if (ok && parts == 4 && (j == n || (!isalnum((unsigned char)in[j]) && !(in[j] == '.' && j + 1 < n && isdigit((unsigned char)in[j + 1]))))) { RD_PUT("<ip>"); i = j; done = 1; } }
#endif
        if (!done) { if (o + 1 < cap) out[o++] = in[i]; i++; }
    }
#ifndef DM_TEST_RUNDIAG_NO_REDACT
    for (size_t a = 0; a < o; a++) if (out[a] == '@') {   // e-mail: name@domain.tld, done on the output (the parts above leave addresses as they were)
        size_t l = a, r = a + 1, dot = 0;
        while (l > 0 && (isalnum((unsigned char)out[l - 1]) || strchr("._+-", out[l - 1]))) l--;
        while (r < o && (isalnum((unsigned char)out[r]) || out[r] == '.' || out[r] == '-')) { if (out[r] == '.') dot = r; r++; }
        while (r > a + 1 && out[r - 1] == '.') r--;
        if (l < a && dot > a + 1 && dot + 2 < r && isalpha((unsigned char)out[dot + 1]) && isalpha((unsigned char)out[r - 1])) {
            const char *ph = "<email>"; size_t pl = strlen(ph), tail = o - r;
            if (l + pl + tail < cap) { memmove(out + l + pl, out + r, tail); memcpy(out + l, ph, pl); o = l + pl + tail; a = l + pl - 1; } }
    }
#endif
#undef RD_PUT
    return o;
}
// Writes text (n bytes) to out redacted line by line; a line longer than 64 KiB is cut into pieces. 0 on success
static inline int rundiag_write_redacted(FILE *out, const char *text, size_t n) {
    static char line[65536 * 2 + 64];
    for (size_t i = 0; i < n;) {
        size_t e = i; while (e < n && text[e] != '\n' && e - i < 65536) e++;
        size_t m = rundiag_redact_line(text + i, e - i, line, sizeof line);
        if (fwrite(line, 1, m, out) != m) return -1;
        if (e < n && text[e] == '\n') { if (fputc('\n', out) == EOF) return -1; e++; }
        i = e;
    }
    return 0;
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
    static char buf[65536 * 2]; size_t have = 0;   // whole lines go to the redaction; a part line waits for the next read
    while (left > 0) {
        ssize_t r = pread(fd, buf + have, (size_t)(left < (off_t)(sizeof buf - have) ? left : (off_t)(sizeof buf - have)), at);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        have += (size_t)r; at += r; left -= r;
        size_t cut = have; while (cut > 0 && buf[cut - 1] != '\n') cut--;
        if (!cut && have == sizeof buf) cut = have;   // one line longer than the buffer: as it is cut
        if (cut && rundiag_write_redacted(out, buf, cut)) { close(fd); return -1; }
        memmove(buf, buf + cut, have - cut); have -= cut;
    }
    if (have && rundiag_write_redacted(out, buf, have)) { close(fd); return -1; }
    close(fd); return ferror(out) ? -1 : 0;
}

// The report at path: head, then the tail of each of the nlogs logs (names in dir); written to path.tmp and renamed, so a half-written report is
// never shared. 0 on success
static inline int rundiag_report(const char *path, const char *head, const char *dir, const char *const *logs, int nlogs, off_t limit) {
    char tmp[1100]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w"); if (!f) return -1;
    int bad = rundiag_write_redacted(f, head, strlen(head)) != 0;
    for (int i = 0; i < nlogs && !bad; i++) { char p[1024]; snprintf(p, sizeof p, "%s/%s", dir, logs[i]); bad = rundiag_tail(f, p, logs[i], limit) != 0; }
    if (fclose(f)) bad = 1;
    if (bad || rename(tmp, path)) { unlink(tmp); return -1; }
    return 0;
}
