// Cube array textures (nvn_mtl.m tex_desc; plan item 198): NVN's depth counts faces, Metal's array length cubes, so a depth of 12 makes 2 cubes
// quietly and a depth of 13 makes 2 with a red line (the 13th face has no slice; blade2-mobile 4ebabb2). Without the line
// (-DDM_TEST_CUBE_DEPTH_SILENT): 1 fails.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-72s expected %-3lld → got %-3lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
static MTLTextureDescriptor *cube(unsigned depth) {
    tex_t t = {.serial = 778}; t.d.width = t.d.height = 16; t.d.depth = depth; t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_CUBEMAP_ARRAY; t.d.format = NVN_FORMAT_RGBA8;
    return tex_desc(&t, 0);
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    MTLTextureDescriptor *d = cube(12);
    EXPECT("depth 12: a cube array of 2 cubes", 2, d.textureType == MTLTextureTypeCubeArray ? (long long)d.arrayLength : -1);
    EXPECT("... and no red line", 0, g_red_once[115]);
    d = cube(13);
    EXPECT("depth 13: still 2 cubes", 2, (long long)d.arrayLength);
    EXPECT("... with the red line (the 13th face has no slice)", 1, g_red_once[115]);
    printf("CUBEARR %s %d/%d\n", fail ? "FAIL" : "OK", count - fail, count);
    return fail != 0;
} }
