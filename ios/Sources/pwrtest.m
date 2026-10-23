// What the iPhone sustains without the game (plan item 230, a measurement): DM_PWRTEST=<threads>[:gpu][:<seconds>] runs, instead of the game, that
// many CPU threads of dependent arithmetic at user-interactive and, with gpu, back-to-back Metal compute dispatches of the same kind, for the seconds
// asked (240 by default), and logs once a second: each thread's share of the performance / efficiency cores and its clock there (thread_selfcounts
// per perf level, as hle_nvn.c), the work done (CPU loop rounds, GPU dispatches, both a second), the process's CPU energy (TASK_POWER_INFO_V2) and the
// thermal state. Tears of the Kingdom's frame chain fell from a 3.9 GHz performance core to 1.4-1.7 GHz about a minute in, thermal state nominal
// (2026-10-23): does a plain load fall the same way, and does a GPU load beside it take the CPU's clock?
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include "pwrspec.h"

void plog(const char *fmt, ...);
extern int thread_selfcounts(int type, void *buf, size_t nbytes);
typedef struct { uint64_t instr, cyc, user, sys; } pt_lvl;   // THSC_TIME_CPI_PER_PERF_LEVEL (4): one per perf level, 0 the fastest
enum { PT_THREADS = 8, PT_LVL = 4 };
static int g_nlvl = 1, g_run = 1;
static pt_lvl g_lvl[PT_THREADS][PT_LVL];   // each thread's latest counters, written by the thread (a torn read only blurs one second)
static _Atomic uint64_t g_rounds[PT_THREADS], g_disp, g_gpu_ns;

static void *cpu_main(void *arg) {
    int me = (int)(intptr_t)arg; pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    uint64_t x = 0x9e3779b97f4a7c15ull + (uint64_t)me; double f = 1.0 + me;
    while (g_run) {
        for (int i = 0; i < 1 << 20; i++) { x ^= x << 13; x ^= x >> 7; x ^= x << 17; f = f * 1.0000001 + (double)(x & 1023) * 1e-9; }
        atomic_fetch_add(&g_rounds[me], 1);
        pt_lvl l[PT_LVL] = {0}; if (!thread_selfcounts(4, l, sizeof *l * (size_t)g_nlvl)) memcpy(g_lvl[me], l, sizeof l);
    }
    if (f == 0 && x == 0) plog("pwrtest: %f", f);   // keeps the loop
    return NULL;
}
static int g_gpu_duty = 100;   // :gpu<N>: the GPU kept about N % busy (one dispatch in flight, a pause after each in proportion to its GPU time)
static void gpu_start(void) {
    id<MTLDevice> dev = MTLCreateSystemDefaultDevice(); NSError *e = nil;
    id<MTLLibrary> lib = [dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal;\n"
        "kernel void k(device float *o [[buffer(0)]], uint i [[thread_position_in_grid]]) { float a = i * 1e-6, b = 1.0001;"
        " for (int n = 0; n < 4096; n++) { a = fma(a, b, 1e-7); b = fma(b, 0.99999, 1e-6); } o[i & 65535] = a + b; }" options:nil error:&e];
    id<MTLComputePipelineState> ps = lib ? [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@"k"] error:&e] : nil;
    if (!ps) { plog("[red] pwrtest: no GPU kernel: %s", e.localizedDescription.UTF8String); return; }
    id<MTLCommandQueue> q = [dev newCommandQueue]; id<MTLBuffer> out = [dev newBufferWithLength:65536 * 4 options:MTLResourceStorageModePrivate];
    dispatch_semaphore_t in_flight = dispatch_semaphore_create(g_gpu_duty < 100 ? 1 : 2); int duty = g_gpu_duty;
    __block _Atomic uint64_t last_ns = 0;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        while (g_run) { @autoreleasepool {
            dispatch_semaphore_wait(in_flight, DISPATCH_TIME_FOREVER);
            if (duty < 100) { uint64_t l = atomic_load(&last_ns); if (l) usleep((useconds_t)(l / 1000 * (uint64_t)(100 - duty) / (uint64_t)duty)); }
            id<MTLCommandBuffer> cb = [q commandBuffer]; id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
            [ce setComputePipelineState:ps]; [ce setBuffer:out offset:0 atIndex:0];
            [ce dispatchThreads:MTLSizeMake(1 << 20, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)]; [ce endEncoding];
            [cb addCompletedHandler:^(id<MTLCommandBuffer> c) {
                uint64_t g = (uint64_t)((c.GPUEndTime - c.GPUStartTime) * 1e9); atomic_store(&last_ns, g);
                atomic_fetch_add(&g_disp, 1); atomic_fetch_add(&g_gpu_ns, g); dispatch_semaphore_signal(in_flight); }];
            [cb commit];
        } }
    });
}
int dm_pwrtest(void) {   // main.m's boot thread: 1 when DM_PWRTEST ran (the game does not start)
    const char *e = getenv("DM_PWRTEST"); pwr_spec sp; if (!pwr_spec_parse(e, &sp)) return 0;
    int nt = sp.threads, gpu = sp.gpu, secs = sp.secs; g_gpu_duty = sp.duty;
    { int v = 0; size_t sz = sizeof v; if (!sysctlbyname("hw.nperflevels", &v, &sz, NULL, 0) && v > 0) g_nlvl = v > PT_LVL ? PT_LVL : v; }
    char names[PT_LVL][32]; int cpus[PT_LVL];
    for (int k = 0; k < g_nlvl; k++) { char key[48]; size_t nz = sizeof names[k]; snprintf(key, sizeof key, "hw.perflevel%d.name", k);
        if (sysctlbyname(key, names[k], &nz, NULL, 0)) snprintf(names[k], sizeof names[k], "level %d", k);
        snprintf(key, sizeof key, "hw.perflevel%d.logicalcpu", k); size_t sz = sizeof cpus[k]; if (sysctlbyname(key, &cpus[k], &sz, NULL, 0)) cpus[k] = -1; }
    plog("pwrtest: DM_PWRTEST=%s: %d CPU threads%s (%d %% busy) for %d s; %s cores %d, %s cores %d", e, nt, gpu ? " and the GPU" : "", gpu ? g_gpu_duty : 0, secs, names[0], cpus[0], g_nlvl > 1 ? names[1] : "-", g_nlvl > 1 ? cpus[1] : 0);
    for (int i = 0; i < nt; i++) { pthread_t t; pthread_create(&t, NULL, cpu_main, (void *)(intptr_t)i); pthread_detach(t); }
    if (gpu) gpu_start();
    mach_timebase_info_data_t tb; mach_timebase_info(&tb);
    pt_lvl prev[PT_THREADS][PT_LVL]; memset(prev, 0, sizeof prev); uint64_t pr[PT_THREADS] = {0}, pd = 0, pg = 0, pe = 0, pt = mach_absolute_time();
    for (int s = 0; s <= secs; s++) {
        uint64_t now = mach_absolute_time(); double dt = (double)(now - pt) * tb.numer / tb.denom / 1e9; pt = now; if (dt <= 0) dt = 1;
        task_power_info_v2_data_t pw; mach_msg_type_number_t n = TASK_POWER_INFO_V2_COUNT; uint64_t en = 0;
        if (task_info(mach_task_self(), TASK_POWER_INFO_V2, (task_info_t)&pw, &n) == KERN_SUCCESS) en = pw.task_energy;
        char l[900]; size_t o = 0; uint64_t rounds = 0;
        for (int i = 0; i < nt && o < sizeof l; i++) {
            pt_lvl cur[PT_LVL]; memcpy(cur, g_lvl[i], sizeof cur); double ms[PT_LVL], tot = 0;
            for (int k = 0; k < g_nlvl; k++) { ms[k] = (double)((cur[k].user + cur[k].sys) - (prev[i][k].user + prev[i][k].sys)) * tb.numer / tb.denom / 1e6; tot += ms[k]; }
            o += (size_t)snprintf(l + o, sizeof l - o, " t%d", i);
            for (int k = 0; k < g_nlvl && o < sizeof l; k++)
                o += (size_t)snprintf(l + o, sizeof l - o, " %c%.0f%%@%.2f", names[k][0], tot > 0 ? 100 * ms[k] / tot : 0, ms[k] > 0 ? (double)(cur[k].cyc - prev[i][k].cyc) / (ms[k] * 1e6) : 0);
            memcpy(prev[i], cur, sizeof cur); uint64_t r = atomic_load(&g_rounds[i]); rounds += r - pr[i]; pr[i] = r;
        }
        uint64_t d = atomic_load(&g_disp), g = atomic_load(&g_gpu_ns);
        if (s) plog("pwrtest %3d s: thermal %ld, CPU energy %.2f W, rounds %.0f/s, GPU dispatches %.1f/s (busy %.0f%%);%s", s, (long)NSProcessInfo.processInfo.thermalState,
                    (en - pe) / 1e9 / dt, rounds / dt, (d - pd) / dt, (g - pg) / 1e7 / dt, l);
        pe = en; pd = d; pg = g;
        sleep(1);
    }
    g_run = 0; usleep(200000);
    plog("pwrtest: done");
    return 1;
}
