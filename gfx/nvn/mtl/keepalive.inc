// gfx/nvn/mtl/keepalive.inc, part of nvn_mtl.m (included there in order, one translation unit): GPU keep-alive (MacNativeGames' TOTK_GPU_KEEPALIVE)
// ---------- GPU keep-alive (MacNativeGames, MIT: TOTK_GPU_KEEPALIVE; written anew here) ----------
// Apple GPUs lower their clock when they idle part of each frame: MacNativeGames measured the same compute work at 10.9 ms back to back, 16.8 ms with
// 10 ms gaps, and Tears of the Kingdom's world scene at GPU p50 12.1 → 9.1 ms with a spinner in the gaps. While the game submits, a thread keeps one
// short spin kernel (one threadgroup, ~1 ms, sized from its measured time) on a queue of its own, only while none of the game's command buffers is on
// the GPU (so a game submission waits for at most one), and stops 100 ms after the last submission. Costs power: on by default on the Mac with a
// window only (not headless, not in the tests); DM_NVN_KEEPALIVE=0 / 1 overrides (t_keepalive)
static _Atomic int g_inflight;                   // the game's command buffers committed and not completed
static dispatch_semaphore_t g_ka_idle;           // signalled when g_inflight drops to 0
static _Atomic uint64_t g_last_sub_ns, g_ka_n, g_ka_us;
static _Atomic int g_ka_target_us = 1000;
int mtl_display_hz(void);
static int keepalive_on(void) {   // default: on with a window on the Mac (not in headless runs and the tests)
    static int on = -1;
    if (on < 0) { const char *e = getenv("DM_NVN_KEEPALIVE");
#if TARGET_OS_IPHONE
        on = e ? atoi(e) != 0 : 0;
#else
        on = e ? atoi(e) != 0 : mtl_display_hz() > 0;
#endif
    }
    return on;
}
static void keepalive_start(void) {
    static int started; if (started || !keepalive_on()) return; started = 1;
    NSError *err = nil;
    id<MTLLibrary> lib = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void keep(device float *a [[buffer(0)]], constant uint &n [[buffer(1)]], uint i [[thread_position_in_grid]]) { float x = a[i]; for (uint j = 0; j < n; j++) x = fma(x, 1.0001f, 0.0001f); a[i] = x; }\n" options:nil error:&err];
    id<MTLComputePipelineState> ps = lib ? [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"keep"] error:&err] : nil;
    id<MTLCommandQueue> q = [g_dev newCommandQueue]; id<MTLBuffer> buf = [g_dev newBufferWithLength:4096 options:MTLResourceStorageModePrivate];
    if (!ps || !q || !buf) { MLOG("[red] GPU keep-alive: no spin kernel (%s)", err.localizedDescription.UTF8String); return; }
    g_ka_idle = dispatch_semaphore_create(0);
    MLOG("GPU keep-alive: short spin kernels (~%d us) on a queue of their own while the GPU would idle between the game's work (DM_NVN_KEEPALIVE=0 turns it off)", atomic_load(&g_ka_target_us));
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        __block uint32_t n = 40000; dispatch_semaphore_t one = dispatch_semaphore_create(1);
        for (;;) @autoreleasepool {
            uint64_t last = atomic_load(&g_last_sub_ns);
            if (!last || clock_gettime_nsec_np(CLOCK_UPTIME_RAW) - last > 100000000ull) { usleep(10000); continue; }   // the game is not rendering
            if (atomic_load(&g_inflight) > 0) { dispatch_semaphore_wait(g_ka_idle, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_MSEC)); continue; }   // its own work keeps the clock up
            dispatch_semaphore_wait(one, DISPATCH_TIME_FOREVER);
            if (atomic_load(&g_inflight) > 0) { dispatch_semaphore_signal(one); continue; }
            uint32_t iters = n;
            id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:ps]; [ce setBuffer:buf offset:0 atIndex:0]; [ce setBytes:&iters length:4 atIndex:1];
            [ce dispatchThreads:MTLSizeMake(32, 1, 1) threadsPerThreadgroup:MTLSizeMake(32, 1, 1)]; [ce endEncoding];
            [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
                double ms = (c.GPUEndTime - c.GPUStartTime) * 1e3; atomic_fetch_add(&g_ka_n, 1); atomic_fetch_add(&g_ka_us, (uint64_t)(ms * 1e3));
                if (ms > 0.01) { double k = (atomic_load(&g_ka_target_us) * .001) / ms; if (k > 2) k = 2; if (k < 0.5) k = 0.5; double m = n * k; n = m < 2000 ? 2000 : m > 400000 ? 400000 : (uint32_t)m; }
                dispatch_semaphore_signal(one); }];
            [cb commit];
        }
    });
}
static void rp_flush(void);
static const char *cctx_snap(void);   // What is in this submission (printed into the failure line on error; defined before nvn_exec)
