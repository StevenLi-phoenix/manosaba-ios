// The present viewport (nvn_mtl.m present_vp, plan item 155): for each of a table of targets (iPhones and iPads in landscape, the Mac's 3456x2234
// full screen, 1080p) and game pictures (1080p, 720p, 540p, 1600x900, 1440x1080), the picture stays whole on the target (origin at 0 or more, the far
// edge inside), within a pixel of its scaled size, centred to a pixel, and a ratio within 1 % fills the target. Centred before rounding
// (-DDM_TEST_PRESENT_VP_OLD, as before): the origin goes to -1 at 2868x1320 and 3456x2234 (Touchable's cases) and 2796x1290, 2556x1179: 2 fail
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define CHECK(c, ...) do { count++; if (c) printf("  ✓ "); else { fail++; printf("  ✗ "); } printf(__VA_ARGS__); printf("\n"); } while (0)
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    static const double tgt[][2] = {{2868, 1320}, {3456, 2234}, {2796, 1290}, {2556, 1179}, {2622, 1206}, {2388, 1668}, {2732, 2048}, {2752, 2064}, {1920, 1080}, {844, 390}, {1366, 1024}};
    static const double pic[][2] = {{1920, 1080}, {1280, 720}, {960, 540}, {1600, 900}, {1440, 1080}};
    int whole = 1, size = 1, centre = 1, fills = 1; char bad[512] = "";
    for (unsigned i = 0; i < sizeof tgt / sizeof *tgt; i++) for (unsigned j = 0; j < sizeof pic / sizeof *pic; j++) {
        double dw = tgt[i][0], dh = tgt[i][1], cw = pic[j][0], ch = pic[j][1], s = fmin(dw / cw, dh / ch);
        MTLViewport v = present_vp(dw, dh, cw, ch);
        int fill = fabs((dw / dh) / (cw / ch) - 1) < 0.01;
        if (fill) { fills &= v.originX == 0 && v.originY == 0 && v.width == dw && v.height == dh; continue; }
        int w_ok = v.originX >= 0 && v.originY >= 0 && v.originX + v.width <= dw && v.originY + v.height <= dh;
        if (!w_ok && strlen(bad) < 400) snprintf(bad + strlen(bad), sizeof bad - strlen(bad), " %gx%g on %gx%g: %g,%g %gx%g;", cw, ch, dw, dh, v.originX, v.originY, v.width, v.height);
        whole &= w_ok;
        size &= fabs(v.width - cw * s) <= 1 && fabs(v.height - ch * s) <= 1;
        centre &= fabs(v.originX - (dw - v.originX - v.width)) <= 1 && fabs(v.originY - (dh - v.originY - v.height)) <= 1;
    }
    CHECK(whole, "every picture whole on its target (origin 0 or more, the far edge inside)%s", bad);
    CHECK(size, "... within a pixel of its scaled size");
    CHECK(centre, "... centred to a pixel");
    CHECK(fills, "a ratio within 1 %% fills the target");
    printf("== %d checks, %d failed ==\n", count, fail); return fail != 0;
} }
