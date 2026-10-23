// Metal execution layer of the NVN null device, see nvn_mtl.h.
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CAMetalDisplayLink.h>
#include <TargetConditionals.h>
#if TARGET_OS_SIMULATOR   // the iOS Simulator SDK has no MetalFX (Xcode 27; plan item 114): no scaler is ever made, the callers keep their own paths
#define DM_METALFX 0
#define FX_SCALER id
#else
#define DM_METALFX 1
#define FX_SCALER id<MTLFXSpatialScalerBase>
#import <MetalFX/MetalFX.h>
#endif
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <dlfcn.h>
#include <TargetConditionals.h>
#include <sys/sysctl.h>
#include <mach/mach.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <setjmp.h>
#include "nvn_mtl.h"
// Compile-time switch DM_NVN_M3=1: the graphics backend uses the older API (ordinary command queue / command buffers / render, compute and copy encoders, buffers bound per slot, each resource referenced by an argument block declared individually),
// target macOS 15 / iOS 18. Default 0 = Metal 4 (macOS 26 / iOS 26). Both branches share the same replay logic, and every divergence is inside #if DM_NVN_M3
#ifndef DM_NVN_M3
#define DM_NVN_M3 0
#endif
#if DM_NVN_M3
#define RENC id<MTLRenderCommandEncoder>
#define CENC id<MTLComputeCommandEncoder>
#define BENC id<MTLBlitCommandEncoder>
#define CBUF id<MTLCommandBuffer>
#define RPD MTLRenderPassDescriptor
#define FB_ON(o, cb) cb
#define M4_ONLY(...)
#define M3_PRESENT(cb, dr) [cb presentDrawable:dr]
#define FB_SEL addCompletedHandler
#define FB_ARG id<MTLCommandBuffer>
#else
#define RENC id<MTL4RenderCommandEncoder>
#define CENC id<MTL4ComputeCommandEncoder>
#define BENC id<MTL4ComputeCommandEncoder>
#define CBUF id<MTL4CommandBuffer>
#define RPD MTL4RenderPassDescriptor
#define FB_ON(o, cb) o
#define M4_ONLY(...) __VA_ARGS__
#define M3_PRESENT(cb, dr) [dr present]
#define FB_SEL addFeedbackHandler
#define FB_ARG id<MTL4CommitFeedback>
#endif
#include "nvn_obj.h"
#include "../tex/nvn_fmt_mtl.h"
#include "../tex/nvn_deswizzle.h"
#include "../tex/nvn_deswizzle_gpu.h"
#include "../tex/nvn_bcdec.h"
#include "../tex/nvn_bc1astc.h"
#include "nvn_trace.h"
#include "nvn_dirty.h"
#include "nvn_refl.h"
#include "nvn_cover.h"
#include "nvn_ts.h"
#include <mach/mach_time.h>
#include "nvn_copy_pitch.h"
#include "nvn_pool_find.h"
#include "nvn_tex_fit.h"
#include "nvn_depth_clear.h"
#include "dmsh_chain.h"   // DMSH_CHAIN: the translation chain version of the B shader library expected by this source tree
#include "nvn_hmap.h"
#include "nvn_pace.h"
#include "core/core.h"   // the replay's decisions in plain C (plan item 264): MLOG / MDIE / RED_ONCE, and the modules below
#include "core/rt.h"
#include "core/prog.h"
#include "core/store_index.h"
#include "core/usnap.h"
#include "core/tbdr.h"
#include "core/pend.h"
#include "core/frame.h"
#include "core/live.h"
#include "core/capture.h"
#include "core/upd.h"
#include "core/state.h"
#include "core/pass.h"
#include "core/defer.h"
#include "core/fuse.h"
#include "core/exec.h"
#include "nvn_backend.h"
#include "../../host/dm.h"
#import <CommonCrypto/CommonDigest.h>
#include <stdarg.h>
#include <mach-o/dyld.h>
#include <libgen.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <errno.h>
#include <execinfo.h>
#include <os/lock.h>
// Function parameters in this file are never retained again in the callee (the caller / global tables already hold them, and lifetime is guaranteed by defer_obj_release): saves a retain / release pair per call on the hot path
#pragma clang attribute push(__attribute__((objc_externally_retained)), apply_to = function)

static void warm_start(void);
static void warm_claim(void);
static void wb_census_tick(void);
static void wb_idle_pass(void);
static void wb_cpu_pass(void);

static id<MTLDevice> g_dev;
static int g_nvn_deswz_gpu;
static id<MTLCommandQueue> g_q;
#if !DM_NVN_M3
static id<MTL4CommandQueue> g_q4;
static dispatch_queue_t g_feedback_q;
#endif
// Only one command stream queue: the game's command buffers and the presents all go to it (Metal 4: g_q4; older API: g_q; in Metal 4 builds g_q only
// serves CPU-synchronous readback / screenshots / teardown). WaitSync and WaitEvent being correct as no-ops rests on this (g_nops in hle_nvn.c):
// every command stream queue is created through here, and a second one aborts
static void stream_queue_once(void) { static int n; if (++n > 1) MDIE("[red] command stream queue #%d created: the no-op WaitSync / WaitEvent rely on a single queue (g_nops in hle_nvn.c); make them real waits before adding a queue", n); }
static _Atomic uint32_t g_present_qos;
#define ALLOC_MAX 8
static int g_alloc_n, g_alloc_cur;   // in-flight slots of the main submission (Metal 4: one command allocator per slot; older API: used only to limit the number of in-flight submissions)
#if !DM_NVN_M3
static id<MTL4CommandAllocator> g_alloc4[ALLOC_MAX];   // Command allocator pool of the main submission: at start pick one the GPU has finished with, create a new one if there is none (wait only at the cap)
static id<MTL4CommandAllocator> g_up_alloc4[3];
static id<MTL4CommandAllocator> g_present_alloc4[3];
static CBUF g_present_cb[3];
static id<MTL4ArgumentTable> g_vt, g_ft, g_ct, g_up_ct;
static id<MTL4ArgumentTable> g_present_args;
#endif
static id<MTLBuffer> g_present_uv[3];
static uint64_t g_present_alloc_fence[3];
static pthread_mutex_t g_pq_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_pq_cv = PTHREAD_COND_INITIALIZER;
static pthread_cond_t g_link_cv = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t g_link_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t g_link_tick_ns, g_link_tick_n;
static uint64_t g_link_dropped;
static int g_pq_inflight;   // Under g_pq_mu: number of presents committed to the GPU whose completion callback has not arrived yet
int g_dm_present_link;   // Present pacing mode (runtime value of DM_PRESENT_LINK, modified directly during same-process alternating measurement): 0 = acquire the drawable itself; 1 = display link at 60 Hz; 2 = display link at 120 Hz, the game presents a frame every other display-link tick
static int g_latency_set = -1;   // DM_PRESENT_LATENCY (nvn_pace.h nvn_latency_setting): n, 0 = auto (2 at a 30 Hz link), -1 = unset (1)
static uint64_t g_pq_done;
static uint64_t g_pq_read;
#if DM_NVN_M3
static id g_res;   // the MTLResidencySet (iOS 18 / macOS 15); nil on iOS 17, where the Metal 3 path's useResource declarations alone make resources resident
#else
static id<MTLResidencySet> g_res;
#endif
static int g_res_dirty;
static id<MTLSharedEvent> g_ev;
static uint64_t g_fence;                        // Highest committed fence value (incremented exclusively by the submit thread)
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv = PTHREAD_COND_INITIALIZER;
static uint64_t g_done;                         // Highest completed fence value (written by the completion callback)
static int g_test_done_hold; static uint64_t g_test_done_held;   // mtl_test_done_hold (t_nvn only)
static _Atomic int g_last_commit_error;
static _Atomic int g_up_pending;
// Waiting for upload batches: the completion handler broadcasts after it clears a counter, the waiter blocks until it reads zero (nxnative e06a8ec's
// up_wait_zero, MIT). These waits polled with usleep(200 / 1000): each one ended up to a millisecond after the batch completed (a loader that waits
// per resource pays it per resource). The timed wait only guards against a missed completion
static pthread_mutex_t g_up_mu = PTHREAD_MUTEX_INITIALIZER; static pthread_cond_t g_up_cv = PTHREAD_COND_INITIALIZER;
static void up_notify(void) { pthread_mutex_lock(&g_up_mu); pthread_cond_broadcast(&g_up_cv); pthread_mutex_unlock(&g_up_mu); }
static void up_wait_zero(_Atomic int *v) {
    pthread_mutex_lock(&g_up_mu);
    while (atomic_load(v)) { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); ts.tv_nsec += 50000000; if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; } pthread_cond_timedwait(&g_up_cv, &g_up_mu, &ts); }
    pthread_mutex_unlock(&g_up_mu);
}

static pthread_mutex_t g_res_mu = PTHREAD_MUTEX_INITIALIZER;   // Dedicated lock for the residency set: res_add is called on paths that hold g_mu (arena_new), so it cannot share g_mu
static NSHashTable *g_res_objects;
#if DM_NVN_M3
// The older API binds by "buffer object + offset", while the replay layer holds GPU addresses: registered buffers are looked up by address range
// (nvn_m3buf.h: a growing, indexed table; it stopped at 512). Registered by weak reference (removed when residency is withdrawn)
#include "nvn_m3buf.h"
static m3buf_tab g_m3_tab; static os_unfair_lock g_m3_lk = OS_UNFAIR_LOCK_INIT;
static void m3_buf_add(id<MTLBuffer> b) {
    os_unfair_lock_lock(&g_m3_lk); int bad = m3buf_add(&g_m3_tab, b.gpuAddress, b.length, (__bridge void *)b); int n = g_m3_tab.n; os_unfair_lock_unlock(&g_m3_lk);
    static int once; if (bad && !once++) MLOG("[red] legacy API buffer table: no memory for buffer %d", n);
    if (n == 513) MLOG("legacy API buffer table: 513 buffers registered (the old table stopped at 512 and dropped the rest)");
}
static void m3_buf_del(id b) { os_unfair_lock_lock(&g_m3_lk); m3buf_del(&g_m3_tab, (__bridge void *)b); os_unfair_lock_unlock(&g_m3_lk); }
static id<MTLBuffer> m3_buf(uint64_t gpu, NSUInteger *off) {
    uint64_t o = 0; os_unfair_lock_lock(&g_m3_lk); void *p = m3buf_find(&g_m3_tab, gpu, &o); id<MTLBuffer> r = (__bridge id<MTLBuffer>)p; os_unfair_lock_unlock(&g_m3_lk);
    *off = (NSUInteger)o; return r;
}
#endif
// Test switch DM_RES_CENSUS=1 (not normally on): records every object that has entered the residency set (weak references); on the tick of the memory readout line it reports the count and size of those "still in the residency set" and those "removed from the residency set but not yet destroyed".
// The latter = GPU memory we no longer use but someone still holds a reference to (for example a texture ID's cached view, or an autorelease pool on a guest thread that is never drained);
// the first three such textures are named, for `leaks --traceTree=<address>`
static NSHashTable *g_res_ever; static int g_res_census = -1;
static uint64_t g_res_noop;   // res_add / res_remove calls that changed nothing (the set already held / did not hold it): no addAllocation, no commit
static void res_add(id<MTLResource> r) {
    if (!r) return;
    pthread_mutex_lock(&g_res_mu);
    if ([g_res_objects containsObject:r]) { g_res_noop++; pthread_mutex_unlock(&g_res_mu); return; }   // a re-upload adds its texture again (nxnative e06a8ec)
    [g_res addAllocation:(id<MTLAllocation>)r];
    if (!g_res_objects) g_res_objects = [NSHashTable weakObjectsHashTable];
    [g_res_objects addObject:r];
#if DM_NVN_M3
    if ([r conformsToProtocol:@protocol(MTLBuffer)]) m3_buf_add((id<MTLBuffer>)r);
#endif
    if (g_res_census < 0) g_res_census = getenv("DM_RES_CENSUS") != NULL;
    if (g_res_census > 0) { if (!g_res_ever) g_res_ever = [NSHashTable weakObjectsHashTable]; [g_res_ever addObject:r]; }
    g_res_dirty = 1;
    pthread_mutex_unlock(&g_res_mu);
}
static void res_remove(id<MTLResource> r) {
    if (!r) return;
    pthread_mutex_lock(&g_res_mu);
    if (![g_res_objects containsObject:r]) { g_res_noop++; pthread_mutex_unlock(&g_res_mu); return; }
    [g_res removeAllocation:(id<MTLAllocation>)r];
    [g_res_objects removeObject:r];
#if DM_NVN_M3
    m3_buf_del(r);
#endif
    g_res_dirty = 1;
    pthread_mutex_unlock(&g_res_mu);
}
// DM_RES_CHECK=1 (diagnostic): if textures / buffers the shader accesses through the argument block are not in the residency set, print the first 40 (textures are attributed to the actual allocation via parent texture / underlying buffer)
static int g_res_check_on;
static void res_check(id r, const char *what, int reg) {
    static int nbad;
    if (!g_res_check_on || !r || nbad >= 40) return;
    int is_tex = [r conformsToProtocol:@protocol(MTLTexture)], is_buf = [r conformsToProtocol:@protocol(MTLBuffer)];
    if (!is_tex && !is_buf) return;   // Samplers etc. are not allocations
    id<MTLResource> a = r; int hops = 0;
    if (is_tex) { id<MTLTexture> t = (id<MTLTexture>)a; while (t.parentTexture) { t = t.parentTexture; hops++; } a = t.buffer ? (id<MTLResource>)t.buffer : (id<MTLResource>)t; }
    pthread_mutex_lock(&g_res_mu); int ok = [g_res_objects containsObject:a]; pthread_mutex_unlock(&g_res_mu);
    if (!ok) { nbad++; id<MTLTexture> t = is_tex ? (id<MTLTexture>)r : nil;
        MLOG("[red] residency missing #%d: %s slot %d %s %p (parent chain %d levels) allocated %lu B storage %lu%s", nbad, what, reg, is_tex ? "texture" : "buffer", (__bridge void *)a, hops, (unsigned long)a.allocatedSize, (unsigned long)a.storageMode,
             t ? [NSString stringWithFormat:@" format %lu %lux%lu type %lu usage %#lx", (unsigned long)t.pixelFormat, (unsigned long)t.width, (unsigned long)t.height, (unsigned long)t.textureType, (unsigned long)t.usage].UTF8String : ""); }
}
static void res_commit(void) {
    pthread_mutex_lock(&g_res_mu);
    if (g_res_dirty) { [g_res commit]; g_res_dirty = 0; }
    pthread_mutex_unlock(&g_res_mu);
}

@interface NvDeferredRelease : NSObject { @public id object; uint64_t fence; int keep_resident; } @end
@implementation NvDeferredRelease @end
static NSMutableArray<NvDeferredRelease *> *g_deferred;
static pthread_mutex_t g_deferred_mu = PTHREAD_MUTEX_INITIALIZER;
// Deferred release has two meanings that must be kept apart (the root of the GPU memory fault on the first frame on entering Travincal):
//   defer_obj_release = this object is retired: drop the reference after the submissions that used it complete, and remove it from the residency set;
//   defer_obj_unref   = only the reference I hold is no longer wanted, the object itself may still be in use (the view cache of a texture ID may hold the base texture itself): only drop the reference, leave the residency set alone.
// Previously only the former existed: when the view cache was invalidated, a texture that 'is still the current storage of some serial' was also removed from the residency set. Invalidation used to happen only when the base texture was really replaced, so it was never hit;
// after the transparency chain's in-place composite swapped the storage of two textures, each of the two live textures was removed once this way, and afterwards they are used as attachments and sampled every frame as usual - the GPU accesses non-resident memory.
static void defer_obj_release_ex(id obj, int keep_resident) {
    if (!obj) return;
    NvDeferredRelease *d = [NvDeferredRelease new]; d->object = obj; d->fence = g_fence + 1; d->keep_resident = keep_resident;
    pthread_mutex_lock(&g_deferred_mu);
    if (!g_deferred) g_deferred = [NSMutableArray array];
    [g_deferred addObject:d];
    pthread_mutex_unlock(&g_deferred_mu);
}
static void defer_obj_release(id obj) { defer_obj_release_ex(obj, 0); }
static void defer_obj_unref(id obj) { defer_obj_release_ex(obj, 1); }
static void defer_drain(void) {
    NSMutableArray<NvDeferredRelease *> *ready = [NSMutableArray array];
    pthread_mutex_lock(&g_deferred_mu);
    for (NSInteger i = g_deferred.count - 1; i >= 0; i--) {
        NvDeferredRelease *d = g_deferred[(NSUInteger)i];
        if (d->fence <= g_done) { [ready addObject:d]; [g_deferred removeObjectAtIndex:(NSUInteger)i]; }
    }
    pthread_mutex_unlock(&g_deferred_mu);
    for (NvDeferredRelease *d in ready) {
        id obj = d->object;
        if (!d->keep_resident && [obj conformsToProtocol:@protocol(MTLResource)]) {
            pthread_mutex_lock(&g_res_mu);
            int registered = [g_res_objects containsObject:obj];
            pthread_mutex_unlock(&g_res_mu);
            if (registered) res_remove((id<MTLResource>)obj);
        }
        d->object = nil;
    }
}

static pthread_mutex_t g_bf_want_mu = PTHREAD_MUTEX_INITIALIZER;
static char **g_bf_want_keys; static size_t g_bf_want_n, g_bf_want_cap; static int g_bf_want_loaded;
// DM_NVN_BF_WANT=<file>: append the key of each fragment shader that would need an output-mapping-only readable-tile variant (dmsh_build.py
// DMSH_BF_WANT reads the same list). Shader keys here carry the library suffix ("<16 hex>_b"); the list holds the bare 16-digit key.
void bf_want_note(const char *shader_key) {
    const char *path = getenv("DM_NVN_BF_WANT");
    if (!path || !shader_key || strlen(shader_key) < 16 || (shader_key[16] && shader_key[16] != '_')) return;
    for (int i = 0; i < 16; i++) if (!((shader_key[i] >= '0' && shader_key[i] <= '9') || (shader_key[i] >= 'a' && shader_key[i] <= 'f') || (shader_key[i] >= 'A' && shader_key[i] <= 'F'))) return;
    char key[17]; memcpy(key, shader_key, 16); key[16] = 0;
    pthread_mutex_lock(&g_bf_want_mu);
    if (!g_bf_want_loaded) {
        FILE *f = fopen(path, "r"); char line[64];
        if (f) { while (fgets(line, sizeof line, f)) { char *e = strpbrk(line, "\r\n"); if (e) *e = 0; if (strlen(line) != 16) continue;
                if (g_bf_want_n == g_bf_want_cap) { g_bf_want_cap = g_bf_want_cap ? g_bf_want_cap * 2 : 64; g_bf_want_keys = realloc(g_bf_want_keys, g_bf_want_cap * sizeof *g_bf_want_keys); }
                g_bf_want_keys[g_bf_want_n++] = strdup(line); } fclose(f); }
        g_bf_want_loaded = 1;
    }
    for (size_t i = 0; i < g_bf_want_n; i++) if (!strcmp(g_bf_want_keys[i], key)) { pthread_mutex_unlock(&g_bf_want_mu); return; }
    if (g_bf_want_n == g_bf_want_cap) { g_bf_want_cap = g_bf_want_cap ? g_bf_want_cap * 2 : 64; g_bf_want_keys = realloc(g_bf_want_keys, g_bf_want_cap * sizeof *g_bf_want_keys); }
    g_bf_want_keys[g_bf_want_n++] = strdup(key);
    FILE *f = fopen(path, "a"); if (f) { fprintf(f, "%s\n", key); fclose(f); }
    pthread_mutex_unlock(&g_bf_want_mu);
}

// The rest of the Metal backend is in gfx/nvn/mtl/, one file per part, included here in this order: they are one translation unit (statics
// and file-scope state shared as before), split by subject so that each part can be read on its own (plan item 261). The order matters: a part
// uses what the parts before it define.
#include "mtl/pools.inc"
#include "mtl/occlusion.inc"
#include "mtl/usnap.inc"
#include "mtl/encoders.inc"
#include "mtl/frame_timing.inc"
#include "mtl/keepalive.inc"
#include "mtl/commit.inc"
#include "mtl/textures.inc"
#include "mtl/upload.inc"
#include "mtl/writeback.inc"
#include "mtl/rtheap.inc"
#include "mtl/live.inc"
#include "mtl/passes.inc"
#include "mtl/tbdr_xv.inc"
#include "mtl/chunks.inc"
#include "mtl/passgpu.inc"
#include "mtl/renderpass.inc"
#include "mtl/shaders.inc"
#include "mtl/descriptors.inc"
#include "mtl/pipeline.inc"
#include "mtl/border_warm.inc"
#include "mtl/args.inc"
#include "mtl/uniform_rename.inc"
#include "mtl/encode_args.inc"
#include "mtl/lookahead.inc"
#include "mtl/store_index.inc"
#include "mtl/tile_merge.inc"
#include "mtl/u8_layers.inc"
#include "mtl/metalfx_scene.inc"
#include "mtl/draw.inc"
#include "mtl/exec.inc"
#include "mtl/shot.inc"
#include "mtl/texdump.inc"
#include "mtl/present_pipe.inc"
#include "mtl/present.inc"
#include "mtl/present_thread.inc"
