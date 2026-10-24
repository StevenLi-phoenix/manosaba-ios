// gfx/nvn/hle/device.inc, part of hle_nvn.c (included there in order, one translation unit): The device
// ---------- Device ----------
NVN(nvnDeviceBuilderSetDefaults, void, NVNdeviceBuilder *b) { memset(b, 0, sizeof(dev_builder_t)); }
NVN(nvnDeviceBuilderSetFlags, void, NVNdeviceBuilder *b, int f) { ((dev_builder_t *)b)->flags = f; }
NVN(nvnDeviceInitialize, NVNboolean, NVNdevice *d, const NVNdeviceBuilder *b) {
    dev_t_ *o = (dev_t_ *)d; memset(o, 0, sizeof *o);
    o->magic = MAGIC('D', 'E', 'V', 0); o->flags = ((const dev_builder_t *)b)->flags;
    if (!dm_game.nvn_c2.ntex) die("game profile %s has no nvn_c2 (c[2] handle table layout of its shaders)", dm_game.id);
    void mtl_set_c2_layout(int tex, int ntex, int samp, int nsamp); mtl_set_c2_layout(dm_game.nvn_c2.tex, dm_game.nvn_c2.ntex, dm_game.nvn_c2.samp, dm_game.nvn_c2.nsamp);
    void mtl_set_depth_mode(int m); mtl_set_depth_mode(dm_game.nvn_depth_mode);
    void mtl_set_upscale_pass(const char *fs, int color, const char *compose_fs, int compose_color);   // (plan item 63)
    mtl_set_upscale_pass(dm_game.upscale_fs, dm_game.upscale_color, dm_game.compose_fs, dm_game.compose_color);
    OLOG("DeviceInitialize %p flags=%#x", (void *)d, o->flags);
    return 1;
}
NVN(nvnDeviceFinalize, void, NVNdevice *d) { ((dev_t_ *)d)->magic = 0; }
NVN(nvnDeviceGetProcAddress, PFNNVNGENERICFUNCPTRPROC, const NVNdevice *d, const char *name) {
    (void)d; return (PFNNVNGENERICFUNCPTRPROC)nvn_lookup(name);
}

// Device properties: values measured on Tegra X1 (nnheaders nvn_DeviceConstantsNX.h, obtained by calling nvnDeviceGetInteger item by item on SMO)
static const int g_devinfo[] = {
    [0x00] = 0, [0x01] = 0,      // API version: from the game profile (device_api), see nvnDeviceGetInteger
    [0x02] = 14, [0x03] = 65536, [0x04] = 256, [0x05] = 8, [0x06] = 16, [0x07] = 4, [0x08] = 16, [0x09] = 32,
    [0x0A] = 16, [0x0B] = 4, [0x0C] = 4, [0x0D] = 4, [0x0E] = 16, [0x0F] = 32, [0x10] = 32, [0x11] = 256, [0x12] = 256,
    [0x13] = 4, [0x14] = 8, [0x15] = 512, [0x16] = 256, [0x17] = 2, [0x18] = 256, [0x19] = 4096, [0x1A] = 131072,
    [0x1B] = 16, [0x1C] = 1536, [0x1D] = 1024, [0x1E] = 64, [0x1F] = 1536, [0x20] = 65535, [0x21] = 65535, [0x22] = 65535,
    [0x23] = 8, [0x24] = 1048576, [0x25] = 4096, [0x26] = 16, [0x27] = 4096, [0x28] = 1, [0x29] = 1, [0x2A] = 1,
    [0x2B] = 262144, [0x2C] = 15, [0x2D] = 2048, [0x2E] = 1, [0x2F] = 1, [0x30] = 14, [0x31] = 5, [0x32] = 1, [0x33] = 8,
    [0x34] = 8, [0x35] = 4, [0x36] = 32, [0x37] = 128, [0x38] = 32, [0x39] = 128, [0x3A] = 65536, [0x3B] = 1, [0x3C] = 4,
    [0x3D] = 16384, [0x3E] = 134217728, [0x3F] = 2048, [0x40] = 16384, [0x41] = 16384, [0x42] = 1, [0x43] = 1, [0x44] = 1,
    [0x45] = 1, [0x46] = 5, [0x47] = 1, [0x48] = 1, [0x49] = 1, [0x4A] = 1, [0x4B] = 1, [0x4C] = 1, [0x4D] = 16,
    [0x4E] = 1024, [0x4F] = 32, [0x50] = 4096, [0x51] = 65536, [0x52] = 65536, [0x53] = 256, [0x54] = 16384,
    [0x55] = 262144, [0x56] = 4096, [0x57] = 1, [0x58] = 4, [0x59] = 2, [0x5A] = 0, [0x5B] = 4096, [0x5C] = 16384,
    [0x5D] = 4096, [0x5E] = 128, [0x5F] = 4096,
};
NVN(nvnDeviceGetInteger, void, const NVNdevice *d, NVNdeviceInfo what, int *out) {
    (void)d;
    unsigned w = (unsigned)what;
    // 0x5F separate sampler binding count and 0x60/0x61 are not in the table measured on SMO: 0x5F is given 4096 per the sampler pool limit, unverified; 0x60/0x61 report a failure and return 0
    if (w >= sizeof g_devinfo / sizeof *g_devinfo) { LOG("[red] nvnDeviceGetInteger unknown item %#x, returning 0", w); *out = 0; return; }
    *out = g_devinfo[w];
    // API version (game.json device_api, default 55.15; DM_NVN_API=major.minor overrides): manosaba reads it without comparing (main 0x173cbd8, 53.311
    // kept from before, unverified); TotK requires major == 55 and minor >= 15 (main 0xbe0438) and otherwise creates no device, which crashes right
    // after (main 0xb9e658). Same field and default as nxnative's device_api (seedds, MIT)
    if (w <= 1) {
        static int api[2] = {-1, -1};
        if (api[0] < 0) {
            const char *e = getenv("DM_NVN_API");
            if (!e || sscanf(e, "%d.%d", &api[0], &api[1]) != 2) { api[0] = dm_game.nvn_api_major; api[1] = dm_game.nvn_api_minor; }
        }
        *out = api[w];
    }
    OLOG("DeviceGetInteger %#x -> %d", w, *out);
}
NVN(nvnDeviceGetImageHandle, NVNimageHandle, const NVNdevice *d, int id) { (void)d; return (NVNimageHandle)id; }
NVN(nvnDeviceRegisterFastClearColor, NVNboolean, NVNdevice *d, const float *c, NVNformat f) { (void)d; (void)c; (void)f; return 1; }   // a fast-clear hint for the console's compression: accepted, nothing to do (nxnative)
NVN(nvnDeviceRegisterFastClearDepth, NVNboolean, NVNdevice *d, float z) { (void)d; (void)z; return 1; }   // same, for depth (TotK main 0xcc8e38)
// Combined texture handle = texture ID | sampler ID << 20, the encoding of the separate handles; the replay layer takes the sampler from the high bits
// of a texture slot where the shader needs one (nvn_mtl.m NVN_RB_SAMPT). From nxnative (seedds, MIT); TotK main 0x2d34cc
NVN(nvnDeviceGetTextureHandle, NVNtextureHandle, const NVNdevice *d, int tex, int samp) { (void)d; return (NVNtextureHandle)((uint64_t)(uint32_t)tex | (uint64_t)(uint32_t)samp << 20); }
// Counter timestamps are written by the replay layer as 19.2 MHz ticks of the guest clock (nvn_mtl.m gpu_ticks: guest_ns × 12 / 625); these convert back to
// and read the same clock, so a counter timestamp, the current time and the guest's system tick can be subtracted from each other (nxnative found
// returning the raw ticks gave garbage; Tears of the Kingdom's frame-rate controller subtracts the system tick, see gpu_ticks)
NVN(nvnDeviceGetTimestampInNanoseconds, uint64_t, const NVNdevice *d, const NVNcounterData *c) { (void)d; return c->timestamp * 625 / 12; }
NVN(nvnDeviceGetCurrentTimestampInNanoseconds, uint64_t, const NVNdevice *d) { (void)d; return guest_ns() * 12 / 625 * 625 / 12; }
// Separate texture / sampler handles: value = descriptor index (the sampler index goes in bit 20 and above, consistent with the bit layout of combined handles)
NVN(nvnDeviceGetSeparateTextureHandle, NVNseparateTextureHandle, const NVNdevice *d, int id) { (void)d; return (NVNseparateTextureHandle){(uint64_t)id}; }
NVN(nvnDeviceGetSeparateSamplerHandle, NVNseparateSamplerHandle, const NVNdevice *d, int id) { (void)d; return (NVNseparateSamplerHandle){(uint64_t)id << 20}; }
NVN(nvnDeviceInstallDebugCallback, void, NVNdevice *d, const PFNNVNDEBUGCALLBACKPROC cb, void *data, uint32_t enable) {
    dev_t_ *o = (dev_t_ *)d; o->dbg_cb = B(enable) ? (void *)cb : NULL; o->dbg_data = data;
}
NVN(nvnDeviceSetWindowOriginMode, void, NVNdevice *d, NVNwindowOriginMode m) { ((dev_t_ *)d)->origin = m; LOG("[nvn] window origin %s", m ? "upper left (same as D3D / Metal)" : "lower left (GL)"); }
// The shaders were built for the profile's convention (game.json depth_mode; near=-w gets z remapped in every vertex shader): another one draws at wrong depths
NVN(nvnDeviceSetDepthMode, void, NVNdevice *d, NVNdepthMode m) {
    ((dev_t_ *)d)->depth_mode = m; LOG("[nvn] depth mode %s", m ? "near=0 (same as D3D / Metal)" : "near=-w (GL)");
    if ((int)m != dm_game.nvn_depth_mode) LOG("[red] the game sets depth mode %d, its shaders were built for %d (game.json depth_mode): geometry lands at wrong depths or is clipped", (int)m, dm_game.nvn_depth_mode);
}
NVN(nvnDeviceFinalizeCommandHandle, void, NVNdevice *d, NVNcommandHandle h) { (void)d; (void)h; }

