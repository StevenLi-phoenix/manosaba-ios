// DM_NVN_DEPTH_PLAIN=1 (nvn_mtl.m tex_desc; plan item 99's A/B): depth and stencil textures are described without the GPU's lossless compression
// (allowGPUOptimizedContents NO), colour textures keep it. Run without the switch (the default) the depth checks fail: 2 fail.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int fail, count;
#define EXPECT(name, want, got) do { long long w=(want), g=(got); count++; printf("%-72s expected %-3lld → got %-3lld %s\n", name, w, g, w==g?"✓":"✗"); fail += w != g; } while (0)
static MTLTextureDescriptor *desc(int format) {
    tex_t t = {.serial = 777}; t.d.width = t.d.height = 64; t.d.depth = t.d.samples = t.d.levels = 1; t.d.target = NVN_TEXTURE_TARGET_2D; t.d.format = format;
    return tex_desc(&t, 0);
}
int main(void) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0); mtl_init();
    EXPECT("DEPTH24_STENCIL8: not GPU-optimized", 0, desc(NVN_FORMAT_DEPTH24_STENCIL8).allowGPUOptimizedContents);
    EXPECT("DEPTH32F: not GPU-optimized", 0, desc(NVN_FORMAT_DEPTH32F).allowGPUOptimizedContents);
    EXPECT("RGBA8: GPU-optimized as before", 1, desc(NVN_FORMAT_RGBA8).allowGPUOptimizedContents);
    printf("DEPTH_PLAIN %s %d/%d\n", fail ? "FAIL" : "OK", count - fail, count);
    return fail != 0;
} }
