// The entropy and random takeovers and nn::pl::GetSharedFont (plan item 64i). getentropy: 256 bytes filled, 257 refused (-1, as both games'
// sdk); the crypto and os random bytes from the same stream: with DM_DETCLOCK the stream's own values in order (getentropy, then crypto, then os).
// GetSharedFont over six plain TTF files in a temporary DM_SYSFONT: six fonts, each SharedFontData (24 bytes) with its type, the address GetSharedFontAddress gives and its size; a smaller array takes the first entries, the count still six. Fonts left out by
// type order (-DDM_TEST_FONT_REVERSED): 2 fail.
#include "../hle/hle_os.c"
#include "../hle/hle_pl.c"
#include <unistd.h>
void crash_watch_frame(void) {}   // host/runtime/crash.c in the host
void dm_guest_clock_advanced(void) {}

static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static uint64_t splitmix(uint64_t *s) { uint64_t z = (*s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }

int main(void) {
    setenv("DM_DETCLOCK", "16666666", 1);
    uint8_t a[257], b[16], c[16]; uint64_t s = 0x5357444554524e47ull, want[6];
    for (int i = 0; i < 6; i++) want[i] = splitmix(&s);
    check("getentropy: 257 bytes refused (-1, as the sdk)", g_getentropy(a, 257) == -1);
    check("... 16 bytes: the stream's first two values", g_getentropy(a, 16) == 0 && !memcmp(a, want, 16));
    h_crypto_random(b, 16); h_os_random(c, 16);
    check("crypto, then os random bytes: the next values of the same stream (DM_DETCLOCK)", !memcmp(b, want + 2, 16) && !memcmp(c, want + 4, 16));

    char dir[512]; snprintf(dir, sizeof dir, "%s/t_rng_font_%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid()); mkdir(dir, 0755); setenv("DM_SYSFONT", dir, 1);
    for (int t = 0; t < NFONT; t++) { char p[700]; snprintf(p, sizeof p, "%s/%s.ttf", dir, g_font[t].file); FILE *f = fopen(p, "wb"); for (int k = 0; k < 100 + t * 4; k++) fputc(t, f); fclose(f); }
    shared_font_data fd[8]; memset(fd, 0xa5, sizeof fd); int cnt = -1;
    int ok = h_pl_GetSharedFont(&cnt, fd, 8, 0);
    int all = ok && cnt == NFONT;
    for (int i = 0; i < NFONT && all; i++)
        all = fd[i].type == (uint32_t)i && fd[i].address == h_pl_Address(i) && fd[i].size == h_pl_Size(i) && fd[i].size == (uint64_t)(100 + i * 4) && ((const uint8_t *)fd[i].address)[0] == i;
    check("GetSharedFont: six fonts, each its type, GetSharedFontAddress's address and its size", all);
    shared_font_data two[3]; memset(two, 0xa5, sizeof two); cnt = -1;
    h_pl_GetSharedFont(&cnt, two, 2, 0);
    uint8_t pad[sizeof two[2]]; memset(pad, 0xa5, sizeof pad);
    check("... into an array of two: the first two, the third left alone, the count still six", cnt == NFONT && two[0].type == 0 && two[1].type == 1 && !memcmp(&two[2], pad, sizeof pad));
    for (int t = 0; t < NFONT; t++) { char p[700]; snprintf(p, sizeof p, "%s/%s.ttf", dir, g_font[t].file); unlink(p); } rmdir(dir);

    unsetenv("DM_DETCLOCK");
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
