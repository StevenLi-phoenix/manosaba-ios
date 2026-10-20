// UTC milliseconds on log lines (logstamp.h, plan item 106): stderr is replaced by the stamping stream over a pipe and written the ways the host
// writes it: LOG's text and newline in two calls, a line in three pieces, two lines in one call, a line written partly before a long pause (the
// stamp is taken when the line starts), a crash-style dump of many fprintf calls; then read back. Every line starts with exactly one stamp of the
// form [YYYY-MM-DDTHH:MM:SS.mmmZ], none appears inside a line, the text is unchanged, the stamps are the current UTC time (within 5 s) and never go
// back. DM_LOG_TIME=0 leaves stderr alone. A stamp before every write (-DDM_TEST_LOGSTAMP_EVERY_WRITE) fails 2.
#include "../logstamp.h"
#include <fcntl.h>
static int n, fail;
#define CHECK(c, ...) do { n++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
static int stamp_at(const char *s, struct tm *u, long *ms) {   // a stamp at s: 27 characters "[2026-10-20T03:04:05.678Z] "
    int Y, M, D, h, m, sec, x; char z, br, sp;
    if (sscanf(s, "[%4d-%2d-%2dT%2d:%2d:%2d.%3d%c%c%c", &Y, &M, &D, &h, &m, &sec, &x, &z, &br, &sp) != 10 || z != 'Z' || br != ']' || sp != ' ' || s[26] != ' ') return 0;
    memset(u, 0, sizeof *u); u->tm_year = Y - 1900; u->tm_mon = M - 1; u->tm_mday = D; u->tm_hour = h; u->tm_min = m; u->tm_sec = sec; *ms = x; return 1;
}
int main(void) {
    int pfd[2]; if (pipe(pfd)) return 2;
    int saved = dup(2); dup2(pfd[1], 2); close(pfd[1]);
    setenv("DM_LOG_TIME", "0", 1); FILE *was = stderr;
    int off = logstamp_stderr(); int left_alone = !off && stderr == was;
    unsetenv("DM_LOG_TIME");
    int on = logstamp_stderr();
    fprintf(stderr, "[dm] LOG text"); fputc('\n', stderr);                                   // LOG: text, then the newline
    fputs("a line ", stderr); fputs("in three ", stderr); fputs("pieces\n", stderr);
    fputs("two lines\nin one call\n", stderr);
    fputs("started, ", stderr); fflush(stderr); usleep(300000); fputs("finished 0.3 s later\n", stderr);
    fprintf(stderr, "[dm] CRASH sig=11"); fprintf(stderr, " pc=main+0x10"); fprintf(stderr, " sp=0x1\n");
    for (int i = 0; i < 6; i++) fprintf(stderr, "x%d=%#x%s", i, i, i % 3 == 2 ? "\n" : " ");
    fflush(stderr); dup2(saved, 2); close(saved); stderr = fdopen(2, "w");
    char buf[8192]; size_t got = 0; ssize_t r;
    fcntl(pfd[0], F_SETFL, O_NONBLOCK);
    while ((r = read(pfd[0], buf + got, sizeof buf - 1 - got)) > 0) got += (size_t)r;
    buf[got] = 0;
    const char *want[] = {"[dm] LOG text", "a line in three pieces", "two lines", "in one call", "started, finished 0.3 s later", "[dm] CRASH sig=11 pc=main+0x10 sp=0x1", "x0=0 x1=0x1 x2=0x2", "x3=0x3 x4=0x4 x5=0x5"};
    int nl = 0, stamped = 0, text_ok = 1, inner = 0, near = 1, mono = 1; time_t now = time(NULL); double prev = 0;
    for (char *s = buf, *e; *s && (e = strchr(s, '\n')); s = e + 1, nl++) {
        *e = 0; struct tm u; long ms;
        if (stamp_at(s, &u, &ms)) { stamped++;
            double t = (double)timegm(&u) + ms / 1000.0; if (t < prev) mono = 0; prev = t; if (t < now - 5 || t > now + 5) near = 0;
            if (nl < 8 && strcmp(s + 27, want[nl])) { text_ok = 0; printf("     line %d: \"%s\"\n", nl, s + 27); }
            if (strstr(s + 27, "Z] ")) inner++;
        } else printf("     unstamped: \"%s\"\n", s);
    }
    CHECK(left_alone, "DM_LOG_TIME=0: stderr left alone");
    CHECK(on, "the stamping stream replaces stderr");
    CHECK(nl == 8 && stamped == 8, "every line starts with a stamp (%d of %d lines)", stamped, nl);
    CHECK(inner == 0, "no stamp inside a line (%d)", inner);
    CHECK(text_ok, "the text of each line is unchanged");
    CHECK(near && mono, "the stamps are the current UTC time and never go back");
    printf("t_logstamp: %d checks, %d failed\n", n, fail); return fail != 0;
}
