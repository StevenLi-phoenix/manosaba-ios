// gfx/nvn/mtl/mtl.h: the Metal backend's private header (plan item 264): the includes, the Metal 4 / Metal 3 macros and the declarations its
// translation units share. nvn_mtl.m is one translation unit with the parts in mtl/*.inc; the parts in mtl/*.m are translation units of their
// own, each ending with `#pragma clang attribute pop` for the push at the end of this header
#pragma once
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
#include "../nvn_mtl.h"
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
#include "../nvn_obj.h"
#include "../../tex/nvn_fmt_mtl.h"
#include "../../tex/nvn_deswizzle.h"
#include "../../tex/nvn_deswizzle_gpu.h"
#include "../../tex/nvn_bcdec.h"
#include "../../tex/nvn_bc1astc.h"
#include "../nvn_trace.h"
#include "../nvn_dirty.h"
#include "../nvn_refl.h"
#include "../nvn_cover.h"
#include "../nvn_ts.h"
#include <mach/mach_time.h>
#include "../nvn_copy_pitch.h"
#include "../nvn_pool_find.h"
#include "../nvn_tex_fit.h"
#include "../nvn_depth_clear.h"
#include "../dmsh_chain.h"   // DMSH_CHAIN: the translation chain version of the B shader library expected by this source tree
#include "../nvn_hmap.h"
#include "../nvn_pace.h"
#include "../core/core.h"   // the replay's decisions in plain C (plan item 264): MLOG / MDIE / RED_ONCE, and the modules below
#include "../core/rt.h"
#include "../core/prog.h"
#include "../core/store_index.h"
#include "../core/usnap.h"
#include "../core/tbdr.h"
#include "../core/pend.h"
#include "../core/frame.h"
#include "../core/live.h"
#include "../core/capture.h"
#include "../core/upd.h"
#include "../core/state.h"
#include "../core/pass.h"
#include "../core/defer.h"
#include "../core/fuse.h"
#include "../core/exec.h"
#include "../core/cond.h"
#include "../nvn_backend.h"
#include "../../../host/dm.h"
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

// ---- Shared between the backend's translation units: defined once, declared here ----
extern id<MTLDevice> g_dev;                          // nvn_mtl.m
// GPU keep-alive (mtl/keepalive.m)
extern _Atomic int g_inflight;                       // the game's command buffers committed and not completed
extern dispatch_semaphore_t g_ka_idle;               // signalled when g_inflight drops to 0
extern _Atomic uint64_t g_last_sub_ns, g_ka_n, g_ka_us;   // the last submission's time; spin kernels run and their GPU microseconds
void keepalive_start(void);                          // on the first submission (when on): the spinning thread
