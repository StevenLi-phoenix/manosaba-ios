// The shared fonts' .bfttf in both word orders (hle_pl.c bfttf_decrypt, plan item 236, after LASRecomp PR #330): a 64-byte stand-in font
// (TrueType version 0x00010000 first) encoded the way of the files we had (words XORed with 0x06186249, magic 0x18029a7f) and the way of the
// console files of LASRecomp issue #48 (words byte-swapped, XORed with 0x49621806, magic 0x7f9a0218) decodes to the same bytes; a wrong magic is
// refused. The first order alone (-DDM_TEST_BFTTF_ONE_ORDER): 1 fails.
#include "../hle/hle_os.c"
#include "../hle/hle_pl.c"
void crash_watch_frame(void) {}
void dm_guest_clock_advanced(void) {}
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-96s %s\n", what, ok ? "✓" : "✗"); }
static void put_le(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
static uint32_t get_le(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
int main(void) {
    uint8_t font[64]; for (int i = 0; i < 64; i++) font[i] = (uint8_t)(i * 7 + 3); font[0] = 0; font[1] = 1; font[2] = 0; font[3] = 0;
    uint8_t a[72], b[72], bad[72];
    put_le(a, 0x18029a7fu ^ 0x06186249u); put_le(a + 4, 64u ^ 0x06186249u);
    for (int i = 0; i < 64; i += 4) put_le(a + 8 + i, get_le(font + i) ^ 0x06186249u);
    put_le(b, 0x7f9a0218u ^ 0x49621806u); put_le(b + 4, 64u ^ 0x49621806u);
    for (int i = 0; i < 64; i += 4) put_le(b + 8 + i, __builtin_bswap32(get_le(font + i)) ^ 0x49621806u);
    memcpy(bad, a, sizeof bad); bad[0] ^= 0x55;
    size_t na = bfttf_decrypt(a, sizeof a), nb = bfttf_decrypt(b, sizeof b), nbad = bfttf_decrypt(bad, sizeof bad);
    check("the files we had (magic 0x18029a7f): 64 bytes, the font's", na == 64 && !memcmp(a, font, 64));
    check("words byte-swapped (magic 0x7f9a0218, LASRecomp issue #48): the same 64 bytes [-DDM_TEST_BFTTF_ONE_ORDER: refused]", nb == 64 && !memcmp(b, font, 64));
    check("a wrong magic: refused", nbad == 0);
    printf("t_bfttf: %d checks, %d failed\n", n, fail); return fail != 0;
}
