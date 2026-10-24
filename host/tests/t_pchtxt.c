// pchtxt patches at load (host/game/pchtxt.c, plan item 150a), on a synthetic product (main of build 0123…cdef: a rewritten word at 0x100, a window
// 0x200-0x220, a region with pieces 0x300-0x310 and 0x340-0x344, a code cave at 0x400, an option row at 0x500-0x504; sdk of another build):
//   a file for main (its @nsobid in upper case, 40 digits): its @enabled lines written, a @disabled section's and those after @stop not, comments
//   ignored; returns 1
//   a patch touching the rewritten word, the window, a region piece, the cave or the option row, or reaching past the image: the whole file refused
//   (-1), nothing of it written
//   a file for another build, or with only 16 digits of build ID: not this module's (0); a malformed line: refused
//   offsets as IPSwitch defines them (plan item 197): without @flag offset_shift an offset counts the NSO header (image offset = offset - 0x100); a
//   later offset_shift applies to the lines after it; "#" lines are information; print_values taken; another flag, or an offset before the image
//   (offset + shift < 0x100), refuses the file
// Written over the rewritten code (-DDM_TEST_PCHTXT_NO_CHECK): 5 fail. Every @flag skipped and "#" not special (-DDM_TEST_PCHTXT_OLD_FLAGS): 5 fail.
#include "../game/pchtxt.c"
#include <sys/stat.h>
#include <unistd.h>
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static char dir[256];
static void put(const char *name, const char *text) { char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name); FILE *f = fopen(p, "w"); fputs(text, f); fclose(f); }
static uint8_t img[0x1000];
static int run(const char *text) { put("p.pchtxt", text); memset(img, 0xee, sizeof img); char p[512]; snprintf(p, sizeof p, "%s/p.pchtxt", dir); return dm_pchtxt_apply(dir, "main", img, 0x1000, p); }
static int untouched(void) { for (size_t i = 0; i < sizeof img; i++) if (img[i] != 0xee) return 0; return 1; }
int main(void) {
    snprintf(dir, sizeof dir, "/tmp/t_pchtxt_%d", (int)getpid()); mkdir(dir, 0700);
    put("main.cuts.tsv", "# BUILD_ID\t0123456789abcdef0123456789abcdef\n");
    put("main.sites.tsv", "0x100\tx18\n0x200\tW\t0x220\n0x300\tR\t0x300-0x310@0x9000 0x340-0x344@0x9100\n");
    put("main.caves.tsv", "0x400\td360fc64\t0x9200\t00000000\t0x404\toutput\twhy\n");
    put("main.options.tsv", "# BUILD_ID\t0123456789abcdef0123456789abcdef\nfps60\t0x500\taabbccdd\t11223344\twhy\n");
    put("sdk.cuts.tsv", "# BUILD_ID\tffffffffffffffffffffffffffffffff\n");
    const char *head = "@nsobid-0123456789ABCDEF0123456789ABCDEF00000000\n@flag offset_shift 0x100\n";
    char t[2048];
    snprintf(t, sizeof t, "%s// a comment\n@enabled\n00000600 01020304\n00000700 AABB // trailing\n@disabled\n00000800 FFFF\n@enabled\n00000900 77\n@stop\n00000a00 66\n", head);
    int r = run(t);
    check("for main: @enabled lines written (0x600, 0x700, 0x900), the @disabled one and those after @stop not; 1",
          r == 1 && img[0x600] == 1 && img[0x603] == 4 && img[0x700] == 0xaa && img[0x701] == 0xbb && img[0x900] == 0x77 && img[0x800] == 0xee && img[0xa00] == 0xee);
    static const struct { const char *what; unsigned off; const char *bytes; } hit[] = {
        {"the rewritten word 0x100", 0x102, "0000"}, {"the window 0x200-0x220", 0x21e, "00000000"}, {"the region piece 0x340-0x344", 0x342, "00"},
        {"the code cave 0x400", 0x400, "1f2003d5"}, {"the option row 0x500-0x504", 0x503, "0000"}};
    for (int i = 0; i < 5; i++) {
        snprintf(t, sizeof t, "%s@enabled\n00000600 01020304\n%08x %s\n", head, hit[i].off, hit[i].bytes); r = run(t);
        char line[160]; snprintf(line, sizeof line, "a patch touching %s: the whole file refused, nothing written", hit[i].what);
        check(line, r == -1 && untouched());
    }
    snprintf(t, sizeof t, "%s@enabled\n00000ff0 000102030405060708090a0b0c0d0e0f1011\n", head); r = run(t);
    check("a patch reaching past the image: refused", r == -1 && untouched());
    r = run("@nsobid-ffffffffffffffffffffffffffffffff\n@enabled\n00000600 01\n");
    int r16 = run("@nsobid-0123456789abcdef\n@enabled\n00000600 01\n");
    check("another build: not main's (0); 16 digits of build ID: not taken (0); nothing written", r == 0 && r16 == 0 && untouched());
    snprintf(t, sizeof t, "%s@enabled\n00000600 0102030\n", head); r = run(t);
    int bad_odd = r == -1; snprintf(t, sizeof t, "%s@enabled\n00000600 \"text\"\n", head); r = run(t);
    check("an odd number of digits, or a string: refused", bad_odd && r == -1 && untouched());
    const char *bid = "@nsobid-0123456789ABCDEF0123456789ABCDEF00000000\n";
    snprintf(t, sizeof t, "%s@enabled\n00000700 A1\n", bid); r = run(t);
    check("197: no offset_shift: the offset counts the NSO header, 0x700 lands at image 0x600", r == 1 && img[0x600] == 0xa1 && img[0x700] == 0xee);
    snprintf(t, sizeof t, "%s@flag offset_shift 0x100\n@enabled\n00000600 B1\n@flag offset_shift 0\n00000700 B2\n@flag offset_shift -0x10\n00000810 B3\n", bid); r = run(t);
    check("197: a later offset_shift applies to the lines after it (0x100: 0x600; 0: 0x700 → 0x600 again; -0x10: 0x810 → 0x700)",
          r == 1 && img[0x600] == 0xb2 && img[0x700] == 0xb3 && img[0x810] == 0xee);
    snprintf(t, sizeof t, "%s#0100F2C0115B6000 a title, 1.4.3\n@flag offset_shift 0x100\n@flag print_values\n@enabled\n# a note inside a section\n00000600 C1\n", bid); r = run(t);
    check("197: \"#\" lines are information and print_values is taken: applied", r == 1 && img[0x600] == 0xc1);
    snprintf(t, sizeof t, "%s@flag offset_shift 0x100\n@flag something_else\n@enabled\n00000600 D1\n", bid); r = run(t);
    check("197: a flag it cannot follow: the file refused, nothing written", r == -1 && untouched());
    snprintf(t, sizeof t, "%s@enabled\n00000080 E1\n", bid); r = run(t);
    check("197: an offset before the image (0x80 without a shift): refused", r == -1 && untouched());
    char cmd[300]; snprintf(cmd, sizeof cmd, "rm -rf %s", dir); if (system(cmd)) {}
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
