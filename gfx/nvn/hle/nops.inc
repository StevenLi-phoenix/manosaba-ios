// gfx/nvn/hle/nops.inc, part of hle_nvn.c (included there in order, one translation unit): Commands with nothing to do here (counted), depth bounds, the no-op list
// Commands that only affect GPU state / only execute on the GPU: the null device only counts them (the thunk has already counted)
static void nvn_nop(void) {}
// Depth bounds (plan item 64c): fragments whose stored depth lies outside [near, far] are discarded, as Metal's setDepthTestMinBound:maxBound:
// does on macOS / iOS 26 (nvn_mtl.m enc_r_depth_bounds); before, they were ignored with a red line
NVN(nvnCommandBufferSetDepthBounds, void, NVNcommandBuffer *cb, uint32_t enable, float n, float f) { *REC(cb, OP_DEPTH_BOUNDS, p_depth_bounds) = (p_depth_bounds){B(enable) != 0, n, f}; }
// Maxwell hardware settings with nothing in Metal to set (plan item 64a): lossy colour reduction (enabling it is reported: Metal stores exactly, which
// is never worse), its thresholds, and 32-bit float depth compression in ZCull (Metal's hidden surface removal is its own)
NVN_NOPROTO(nvnCommandBufferSetColorReductionEnable, void, NVNcommandBuffer *c, uint32_t on) { (void)c; if (B(on)) { static int once; if (!once++) LOG("[nvn] CommandBufferSetColorReductionEnable(1): Metal has no lossy colour reduction, colours stored exactly"); } }
NVN_NOPROTO(nvnCommandBufferSetColorReductionThresholds, void, NVNcommandBuffer *c, int format_class, int a, int b) { (void)c; (void)format_class; (void)a; (void)b; }
NVN_NOPROTO(nvnCommandBufferSetZCullZF32CompressionEnable, void, NVNcommandBuffer *c, uint32_t on) { (void)c; (void)on; }
// Automatic GPU checkpoints are a debugging aid of the console; accepted (bayonetta3-mobile read the shipped SDK's version: it returns 1)
NVN_NOPROTO(nvnDeviceSetAutomaticCheckpointsMask, NVNboolean, NVNdevice *d, int mask) { (void)d; (void)mask; return 1; }
// SetPatchSize, MultiDrawArrays / ElementsIndirectCount, DispatchComputeIndirect and DownsampleTextureView used to be listed here, so a game calling
// them lost draws, dispatches or a downsample without a line: they go to the red-reporting stub now (upstream d2r-mobile 44a6770; nxnative has
// DispatchComputeIndirect / DrawElementsIndirect and Downsample as a resolve, for when a game calls them). SetDepthBounds is above (plan item 64c)
static const char *const g_nops[] = {
    "nvnCommandBufferWaitSync",    // A GPU-side wait on a sync object (CPU waits go through nvnSyncWait, which really waits on the fence). Sync objects come only
                                   // from QueueFenceSync and WindowAcquireTexture, whose fence value is "the highest already submitted when recorded", and the
                                   // command buffer holding WaitSync is submitted after it to the same queue. There is one Metal command stream queue
                                   // (stream_queue_once in nvn_mtl.m): Metal 4 puts a "wait for all earlier stages on the queue" barrier at the start of each
                                   // encoder, and the older API tracks every resource (useResource for argument block references), so both are already in
                                   // order and a no-op is correct. A second queue (async compute, say) would make this a real wait
    "nvnCommandBufferSetShaderScratchMemory",
    "nvnCommandBufferPushDebugGroupDynamic",
    "nvnCommandBufferPopDebugGroup",
    "nvnCommandBufferInsertDebugMarkerDynamic",
    "nvnCommandBufferWaitEvent",   // Waits for an earlier SignalEvent in the same command stream or a CPU EventSignal. One queue executes in order and the GPU
                                   // never passes earlier commands, so a no-op is correct (waiting for a signal issued later would hang a real Switch too)
    "nvnWindowFinalize",
    "nvnCommandBufferSaveZCullData", "nvnCommandBufferRestoreZCullData",   // ZCull: see the textures section
    "nvnCommandBufferSetPointSize",   // Point size comes from the shader's [[point_size]] output (handled by the translation chain)
    // Debug labels: only shown by graphics debuggers
    "nvnDeviceSetDebugLabel", "nvnQueueSetDebugLabel", "nvnWindowSetDebugLabel", "nvnProgramSetDebugLabel",
    "nvnMemoryPoolSetDebugLabel", "nvnTexturePoolSetDebugLabel", "nvnSamplerPoolSetDebugLabel", "nvnBufferSetDebugLabel",
    "nvnTextureSetDebugLabel", "nvnSamplerSetDebugLabel", "nvnCommandBufferSetDebugLabel", "nvnSyncSetDebugLabel",
};
__attribute__((constructor)) static void nvn_nops_register(void) {
    // Empty implementations overwrite the unimplemented stubs after the slot table is initialized; a misspelled name (not in main's name table) reports a failure and aborts at startup
    nvn_init_slots();
    for (unsigned k = 0; k < sizeof g_nops / sizeof *g_nops; k++) {
        unsigned i = 0;
        while (i < NVN_NFUNC && strcmp(nvn_names[i], g_nops[k])) i++;
        if (i == NVN_NFUNC) die("NVN no-op name %s is not in main's name table", g_nops[k]);
        if (nvn_slot[i].fn != (void *)nvn_unimpl_entry) die("NVN %s has an implementation and is also listed as a no-op", g_nops[k]);
        nvn_slot[i].fn = (void *)nvn_nop;
    }
    unsigned nnop = sizeof g_nops / sizeof *g_nops;
    // DM_NVN_PROF=1: implemented functions (except unimplemented stubs and present: present includes the display-link wait and is counted separately) get a timing wrapper, used to break down the time spent in the "translation layer recording phase"
    if (getenv("DM_NVN_PROF"))
        for (unsigned i = 0; i < NVN_NFUNC; i++)
            if (nvn_slot[i].fn != (void *)nvn_unimpl_entry && strcmp(nvn_names[i], "nvnQueuePresentTexture")) { nvn_real[i] = nvn_slot[i].fn; nvn_slot[i].fn = (void *)nvn_prof_entry; }
    LOG("[nvn] null device: %u names, %u implemented (%u semantic + %u no-op), the remaining %u go to the red-reporting stub",
        NVN_NFUNC, g_nsem + nnop, g_nsem, nnop, NVN_NFUNC - g_nsem - nnop);
}

