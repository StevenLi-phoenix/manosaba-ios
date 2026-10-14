// nn::vi（显示层）：真机经 vi 服务开显示、建图层、取原生窗口句柄；句柄交给 NVN 的窗口 builder。
// 宿主：返回宿主静态对象；接 Metal 时原生窗口对应前端的 CAMetalLayer（front_metal_layer()）。
// nn::vi (display layer): on a real device, the vi service is used to open the display, create a layer and get the native window handle; the handle is passed to NVN's window builder.
// Host: returns host static objects; when hooking up Metal, the native window corresponds to the front end's CAMetalLayer (front_metal_layer()).
#include "../host/hle.h"

static struct { uint32_t magic; } g_display = {0x50534944}, g_layer = {0x5259414c};
static struct { uint32_t magic; void *layer; } g_native = {0x4e49574e, 0};

HLE_FORCE("_ZN2nn2vi10InitializeEv", "显示服务初始化；宿主无 vi 服务", void, h_vi_Init, void) {}
HLE_FORCE("_ZN2nn2vi18OpenDefaultDisplayEPPNS0_7DisplayE", "开默认显示；宿主给静态对象", Result, h_vi_OpenDisp, void **out) {
    *out = &g_display; return RES_OK;
}
HLE_FORCE("_ZN2nn2vi11CreateLayerEPPNS0_5LayerEPNS0_7DisplayE", "建图层；宿主给静态对象", Result, h_vi_CreateLayer, void **out, void *disp) {
    (void)disp; *out = &g_layer; return RES_OK;
}
HLE_FORCE("_ZN2nn2vi15GetNativeWindowEPPvPNS0_5LayerE", "原生窗口句柄，交给 NVN 窗口 builder；宿主给静态对象", Result, h_vi_GetNative, void **out, void *layer) {
    (void)layer; *out = &g_native; return RES_OK;
}

// Display vsync event (games that pace frames on it, e.g. Unity): bound on the caller's guest thread, then signaled by a host thread at the
// display rate (60 Hz). Auto-clear, like the real vsync event: each wait consumes one tick
#include <pthread.h>
#include <time.h>
static void *g_vsync_ev; static pthread_once_t g_vsync_once = PTHREAD_ONCE_INIT;
static void *vsync_thread(void *a) {
    (void)a; uint64_t next = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    for (;;) {
        next += 16666667;   // absolute schedule: sleep overshoot does not accumulate
        uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
        if (next > now) { struct timespec r = {(time_t)((next - now) / 1000000000ull), (long)((next - now) % 1000000000ull)}; nanosleep(&r, NULL); }
        else if (now - next > 100000000ull) next = now;   // fell far behind (process suspended): resync instead of bursting
        hle_signal_system_event(g_vsync_ev);
    }
    return NULL;
}
static void vsync_start(void) { pthread_t th; if (pthread_create(&th, NULL, vsync_thread, NULL)) die("vsync 线程起不来"); pthread_detach(th); }
HLE_FORCE_OPT("_ZN2nn2vi20GetDisplayVsyncEventEPNS_2os15SystemEventTypeEPNS0_7DisplayE", "显示垂直同步事件：绑内核事件，宿主线程按 60 Hz 触发", Result, h_vi_VsyncEv, void *ev, void *disp) {
    (void)disp;
    if (g_vsync_ev && g_vsync_ev != ev) LOG("【红】vi::GetDisplayVsyncEvent 第二个事件 %p（只驱动第一个 %p）", ev, g_vsync_ev);
    if (!g_vsync_ev) { hle_bind_system_event(ev, 1); g_vsync_ev = ev; pthread_once(&g_vsync_once, vsync_start); }
    return RES_OK;
}
