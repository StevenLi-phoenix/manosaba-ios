// gfx/nvn/mtl/args.inc, part of nvn_mtl.m (included there in order, one translation unit): Argument buffers: dummy resources, bindings, the driver segment fill
// Argument buffer: filled item by item per the B reflection (uniform slot r, separate texture / sampler slot r, storage buffer slot r, driver segments c0 / c2, c1, support_buffer)
// Unbound resources read 0: give all-zero dummy resources (NVN descriptors 0..255 are reserved for the driver; the game binding handle 0 means 'this slot is empty')
static uint64_t g_dummy_tex_rid[8][4], g_dummy_img_rid[8][4], g_dummy_tbuf_rid[4];
static id<MTLTexture> dummy_tex(int dtt, int dpf) {
    static __strong id<MTLTexture> cache[8][4];
    MTLTextureType tt = (MTLTextureType)dtt;
    MTLPixelFormat pf = dpf == NVN_DPF_DEPTH ? MTLPixelFormatDepth32Float : dpf == NVN_DPF_UINT ? MTLPixelFormatRGBA32Uint : dpf == NVN_DPF_SINT ? MTLPixelFormatRGBA32Sint : MTLPixelFormatRGBA8Unorm;
    id<MTLTexture> t = cache[dtt & 7][dpf & 3];
    if (t) return t;
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.textureType = tt; d.pixelFormat = pf; d.width = d.height = d.depth = 1; d.storageMode = MTLStorageModeShared; d.usage = MTLTextureUsageShaderRead;
    t = [g_dev newTextureWithDescriptor:d];   // Shared storage is all zeros when newly created
    if (pf != MTLPixelFormatDepth32Float) { uint8_t z[64] = {0}; NSUInteger faces = tt == MTLTextureTypeCube || tt == MTLTextureTypeCubeArray ? 6 : 1;
        for (NSUInteger f = 0; f < faces; f++) [t replaceRegion:MTLRegionMake3D(0, 0, 0, 1, 1, 1) mipmapLevel:0 slice:f withBytes:z bytesPerRow:16 bytesPerImage:16]; }
    cache[dtt & 7][dpf & 3] = t; g_dummy_tex_rid[dtt & 7][dpf & 3] = t.gpuResourceID._impl; res_add(t);
    return t;
}
// Write sink for unbound image slots: kept separate from the dummy textures that read 0 (writes must not pollute them); one is shared per same type and same pixel class
static id<MTLTexture> dummy_img(int dtt, int dpf) {
    static __strong id<MTLTexture> cache[8][4];
    id<MTLTexture> t = cache[dtt & 7][dpf & 3]; if (t) return t;
    MTLTextureDescriptor *d = [MTLTextureDescriptor new];
    d.textureType = (MTLTextureType)dtt; d.pixelFormat = dpf == NVN_DPF_UINT ? MTLPixelFormatRGBA32Uint : dpf == NVN_DPF_SINT ? MTLPixelFormatRGBA32Sint : MTLPixelFormatRGBA16Float;
    d.width = d.height = d.depth = 1; d.storageMode = MTLStorageModePrivate; d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    t = [g_dev newTextureWithDescriptor:d]; cache[dtt & 7][dpf & 3] = t; g_dummy_img_rid[dtt & 7][dpf & 3] = t.gpuResourceID._impl; res_add(t); return t;
}
static id<MTLBuffer> dummy_buf(void);
static id<MTLTexture> dummy_tbuf(int dpf) {   // Unbound typed buffer: all zeros (pixel class matches the shader's texture_buffer<T>)
    static __strong id<MTLTexture> cache[4];
    id<MTLTexture> t = cache[dpf & 3]; if (t) return t;
    MTLPixelFormat pf = dpf == NVN_DPF_UINT ? MTLPixelFormatRGBA32Uint : dpf == NVN_DPF_SINT ? MTLPixelFormatRGBA32Sint : MTLPixelFormatRGBA32Float;
    id<MTLBuffer> b = dummy_buf();
    MTLTextureDescriptor *d = [MTLTextureDescriptor textureBufferDescriptorWithPixelFormat:pf width:b.length / 16 resourceOptions:b.resourceOptions usage:MTLTextureUsageShaderRead];
    t = [b newTextureWithDescriptor:d offset:0 bytesPerRow:b.length];
    cache[dpf & 3] = t; g_dummy_tbuf_rid[dpf & 3] = t.gpuResourceID._impl; return t;
}
static id<MTLSamplerState> dummy_samp(void) {
    static id<MTLSamplerState> s; if (!s) { MTLSamplerDescriptor *d = [MTLSamplerDescriptor new]; d.supportArgumentBuffers = YES; s = [g_dev newSamplerStateWithDescriptor:d]; }
    return s;
}
static uint64_t dummy_samp_rid(void) { static uint64_t rid; if (!rid) rid = dummy_samp().gpuResourceID._impl; return rid; }
static id<MTLBuffer> dummy_buf(void) { static id<MTLBuffer> b; if (!b) { b = [g_dev newBufferWithLength:64 << 10 options:MTLResourceStorageModeShared]; res_add(b); } return b; }
static uint64_t dummy_buf_gpu(void) { static uint64_t gpu; if (!gpu) gpu = dummy_buf().gpuAddress; return gpu; }
// useResource is idempotent for the same (resource, usage, stage) within one encoder: remember what this render encoder has already declared and skip repeats (a compute encoder is created anew for each dispatch, no dedup)
static uint8_t g_args_dirty[6] = {1, 1, 1, 1, 1, 1};
static int g_render_off;   // SetRenderEnable(false) is in effect (NVN reference: clears and draws are dropped while it is)
// Conditional rendering counted (plan item 112's measure): conditionals, those on a result still on the GPU (rendering kept on), and the draws and
// clears replayed while one of those was in effect; a line every 300 frames when there were any
static uint64_t g_rc_n, g_rc_pending, g_rc_pend_draws;
static void rc_report(uint64_t f) {
    static uint64_t p[3]; uint64_t c[3] = {g_rc_n, g_rc_pending, g_rc_pend_draws};
    if (f % 300 || c[0] == p[0]) return;
    MLOG("conditional rendering up to frame %llu: %llu conditionals, %llu of them on a samples-passed result still on the GPU (rendering kept on), %llu draws and clears under those",
         (unsigned long long)f, (unsigned long long)(c[0] - p[0]), (unsigned long long)(c[1] - p[1]), (unsigned long long)(c[2] - p[2]));
    memcpy(p, c, sizeof p);
}
static int g_rc_on_pending;   // the conditional in effect was on a pending result
// SetRenderEnableConditional compares the 64-bit values at counters and counters + 16 (the NVN reference: two counter reports, one after the other) and
// renders if they are equal (mode 2) or not equal (mode 3). The host decides when it replays the command, from guest memory: counter reports other
// than samples passed are written in command order at replay (OP_REPORT_COUNTER), so a report earlier in the same submission is seen. A samples-passed
// result exists only after the GPU completes its submission: while one is pending on either value, rendering stays enabled (drawing too much is safe).
// Tears of the Kingdom clears each of its lighting buffers once per frame this way: conditional (not equal) clear, then a TIMESTAMP_TOP report into
// the same counters, whose counter half is 0 (the NVN reference: timestamp reports write 0 there), so the next conditional clear of the frame is
// skipped and the second lighting pass adds to the first. Clearing every time had wiped the first pass's light: the title mural stayed black.
// Samples-passed report destinations: address → fence of the latest submission that writes it (open addressing, one entry per distinct address,
// grown at half load; replay thread only). This was a ring of the last 256 reports: with more reports in flight a destination still pending had
// left the ring and its previous result was read (t_nvn 6c2b). Tears of the Kingdom makes about one report a frame (3 destinations), so it never
// hit that.
typedef struct { uint64_t addr, fence; } occ_ent;
static occ_ent *g_occ; static unsigned g_occ_cap, g_occ_n;
static occ_ent *occ_slot(uint64_t addr) {   // the entry of addr, or the empty slot where it goes
    unsigned m = g_occ_cap - 1, h = (unsigned)((addr >> 3) * 0x9E3779B97F4A7C15ull >> 40) & m;
    while (g_occ[h].addr && g_occ[h].addr != addr) h = (h + 1) & m;
    return &g_occ[h];
}
static void occ_note(uint64_t addr, uint64_t fence) {
    if (!addr) return;
    if (2 * (g_occ_n + 1) > g_occ_cap) {
        occ_ent *old = g_occ; unsigned oc = g_occ_cap; g_occ_cap = oc ? oc * 2 : 1024; g_occ = calloc(g_occ_cap, sizeof *g_occ); if (!g_occ) MDIE("occlusion table");
        for (unsigned i = 0; i < oc; i++) if (old[i].addr) *occ_slot(old[i].addr) = old[i];
        free(old);
    }
    occ_ent *e = occ_slot(addr); if (!e->addr) { e->addr = addr; g_occ_n++; }
    e->fence = fence;
}
static int occ_pending(uint64_t addr) {
    if (!g_occ_cap) return 0;
    pthread_mutex_lock(&g_mu); uint64_t done = g_done; pthread_mutex_unlock(&g_mu);
    const occ_ent *e = occ_slot(addr);
    return e->addr && e->fence > done;
}
static uint64_t g_args_reg_seen, g_args_dirty_seen;
static inline void args_dirty_all(void) { memset(g_args_dirty, 1, sizeof g_args_dirty); }
static inline void args_sync_dirty(void) {
    uint64_t reg = g_reg_gen, dirty = nvn_dirty_now();
    if (reg != g_args_reg_seen || dirty != g_args_dirty_seen) {
        g_args_reg_seen = reg; g_args_dirty_seen = dirty; args_dirty_all();
    }
}
static void bind_args(id<MTLCommandEncoder> en, int nvn_stage, int metal_vertex, id<MTLBuffer> ab, uint64_t gpu, size_t off, NSUInteger ai) {
    if (nvn_stage == 5) enc_c_args((id<MTLComputeCommandEncoder>)en, gpu, ai);
    else enc_r_args((id<MTLRenderCommandEncoder>)en, metal_vertex ? MTLRenderStageVertex : MTLRenderStageFragment, gpu, ai);
}
// Static constant buffers of route B: the Ryujinx support_buffer (layout in Ryujinx.Graphics.Shader/SupportBuffer.cs: alpha_test u32, is_bgra u32[8],
// viewport_inverse f4, viewport_size f4, frag_scale_count i32, render_scale f32[73]..., each field aligned to 16 bytes) and the all-0 driver constant segment (64 KB = the upper bound declared by shaders)
static id<MTLBuffer> support_buf(void) {
    static __strong id<MTLBuffer> b; if (b) return b;
    b = [g_dev newBufferWithLength:65536 options:MTLResourceStorageModeShared]; res_add(b);   // All 0 when newly created
    const int render_scale_off = 192;   // Measured MSL layout: alpha_test 0, is_bgra[8] 16, viewport_inverse 144, viewport_size 160, frag_scale_count 176, render_scale float4[73] 192 (take .x)
    for (int i = 0; i < 73; i++) *(float *)((uint8_t *)b.contents + render_scale_off + 16 * i) = 1.0f;
    return b;
}
static uint64_t support_gpu(void) { static uint64_t gpu; if (!gpu) gpu = support_buf().gpuAddress; return gpu; }
static id<MTLBuffer> zero_bank(void) { static __strong id<MTLBuffer> b; if (!b) { b = [g_dev newBufferWithLength:65536 options:MTLResourceStorageModeShared]; res_add(b); } return b; }
static uint64_t zero_bank_gpu(void) { static uint64_t gpu; if (!gpu) gpu = zero_bank().gpuAddress; return gpu; }
#if DM_NVN_M3
// De-duplicating resource declarations on the older API: repeated useResource for the same resource within one render encoder is idempotent, yet every newly encoded argument block declares all the resources it references once more -
// measured at 9.7 declarations per draw, of which 9.2 were already declared in this encoder, about 227 instructions per draw. Here it is recorded which resources have been declared with which usages in this pass (g_pass.serial, one-to-one with render encoders:
// the serial only increments before a render encoder is opened), and those already covered are skipped. When the table is full (more distinct resources in one pass than the table holds) nothing is recorded and declarations proceed as usual.
// Table entries do not retain resources: a declared resource is held by the encoder until the submission completes, so within one pass its address is not reused by another resource. Used only on the submission thread
enum { M3_USE_N = 1024 };
static struct { void *p; uint64_t pass; uint32_t u; } g_m3_use[M3_USE_N];
static inline int m3_use_seen(void *p, uint32_t u) {
    for (uint32_t h = (uint32_t)(((uintptr_t)p >> 4) * 0x9E3779B1u) >> 22, k = 0; k < M3_USE_N; k++, h = (h + 1) & (M3_USE_N - 1)) {
        if (g_m3_use[h].pass != g_pass.serial) { g_m3_use[h].p = p; g_m3_use[h].pass = g_pass.serial; g_m3_use[h].u = u; return 0; }
        if (g_m3_use[h].p == p) { if ((g_m3_use[h].u & u) == u) return 1; g_m3_use[h].u |= u; return 0; }
    }
    return 0;
}
#endif
// NVN driver constant segments as GLSLC-compiled shaders read them (0xA00 bytes, filled from the current bindings). c0: the storage buffer table
// (16 slots of {address 64 bits, size 32 bits}) at 0x110 (vertex), 0x310 (compute), 0x510 (pixel); for compute also the dispatch size {x, y, z}
// at 0x200 (gl_NumWorkGroups) and the uniform buffer table at 0x210 (16 × {address, size}, up to the storage table); for the graphics stages the
// largest point size at 0x610, to which a shader-written gl_PointSize is clamped (0x44fffe00 = 2047.9375 per MacNativeGames; nxnative uses
// 2047, Maxwell's limit; Metal clamps to its own). c2: the texture and sampler handle tables of the profile (nvn_c2). The compute and point-size
// words: nxnative 9579030 / ca7216c and MacNativeGames' driver_bank_fill (both MIT): Link's Awakening's light culling and Tears of the
// Kingdom's occlusion culling multiplied by a zero workgroup count, Odyssey's point sprites were clamped to size 0
static uint32_t g_dispatch_grid[3];   // the current dispatch's workgroup counts (dispatch)
static uint64_t g_disp_n, g_disp_shared;   // dispatches; those that went into an encoder already open (Metal 4)
static void drv_fill(uint8_t *d, int bank, int stage) {
    memset(d, 0, 0xA00);
    if (stage < 0 || stage >= 6) return;
    if (bank == 0) {
        int base = stage == 0 ? 0x110 : stage == 5 ? 0x310 : stage == 1 ? 0x510 : -1;
        if (base < 0) RED_ONCE(26, "stage %d shader reads driver segment c0, storage buffer table base address not measured", stage);
        else for (int r = 0; r < 16; r++) { uint64_t a = S.ssbo[stage][r].addr; uint32_t n = (uint32_t)S.ssbo[stage][r].size; memcpy(d + base + 16 * r, &a, 8); memcpy(d + base + 16 * r + 8, &n, 4); }
        if (stage == 5) {
            memcpy(d + 0x200, g_dispatch_grid, sizeof g_dispatch_grid);
            for (int r = 0; r < 16; r++) { uint64_t a = S.ubo[5][r].addr; uint32_t n = (uint32_t)S.ubo[5][r].size; memcpy(d + 0x210 + 16 * r, &a, 8); memcpy(d + 0x210 + 16 * r + 8, &n, 4); }
        } else { const float point_max = 2047.9375f; memcpy(d + 0x610, &point_max, sizeof point_max); }
    } else {
        for (int r = 0; r < g_c2.ntex && r < 128; r++) { uint32_t th = (uint32_t)S.tex[stage][r]; memcpy(d + g_c2.tex + 8 * r, &th, 4); }
        for (int r = 0; r < g_c2.nsamp && r < 128; r++) { uint32_t sh_ = (uint32_t)S.samp[stage][r]; memcpy(d + g_c2.samp + 8 * r, &sh_, 4); }
    }
}
