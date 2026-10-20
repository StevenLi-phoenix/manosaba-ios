// nn::hid six-axis sensors and vibration devices (plan item 67): the controller the operation mode connects (hle_hid.c) has both, fed by the
// front end (dm_input.h: dm_motion_set from a controller's or the phone's motion sensors, dm_vibration_take for its haptics).
// Handles are the SDK's own encoding, read from Tears of the Kingdom 1.4.3's sdk (utility::GetVibrationDeviceHandles at 0x51e2b0,
// utility::GetSixAxisSensorHandle at 0x51dd20; dr3-mobile's notes, MIT, found the same in its SDK): {type, Npad ID's low byte, device index, 0},
// type 3 full key, 4 handheld, 5 Joy-Con pair, 6 left, 7 right. Vibration: two devices (index 0 left, 1 right) for full key / handheld / pair, one
// for a single Joy-Con (left {6, id, 0}, right {7, id, 1}); six-axis: one for full key and handheld (index 2), two for a pair, one for a single
// Joy-Con. The game asks for every style and sends to all of them (zeros to the ones not connected, digitalhivehub 7db9248): only the connected
// controller's devices reach the front end. Bool arguments come from Switch code, which leaves the upper bits of the register as they were: only
// the low byte is read (totk-mobile's finding with Tears of the Kingdom's PermitVibration).
#include "hle.h"
#include "dm_input.h"
#include <math.h>
#include <pthread.h>

enum { ST_FULLKEY = 1u << 0, ST_HANDHELD = 1u << 1, ST_JOYDUAL = 1u << 2, ST_JOYLEFT = 1u << 3, ST_JOYRIGHT = 1u << 4 };
uint32_t hle_hid_connected_npad(void);   // hle_hid.c: 0 (No.1, docked) or 0x20 (handheld)
static int npad_ok(uint32_t id) { return id < 8 || id == 0x10 || id == 0x20; }
static uint32_t hdl(uint32_t type, uint32_t id, uint32_t dev) { return type | (id & 0xff) << 8 | dev << 16; }
// The handles of one style (exactly one style bit, as the SDK requires); 0 for the styles the host never connects
int dm_vib_handles(uint32_t *out, int max, uint32_t id, uint32_t style) {
    if (!out || max <= 0 || !npad_ok(id)) return 0;
    uint32_t t = style == ST_FULLKEY ? 3 : style == ST_HANDHELD ? 4 : style == ST_JOYDUAL ? 5 : style == ST_JOYLEFT ? 6 : style == ST_JOYRIGHT ? 7 : 0;
    if (!t) return 0;
    if (t == 6) { out[0] = hdl(6, id, 0); return 1; }
    if (t == 7) { out[0] = hdl(7, id, 1); return 1; }
    out[0] = hdl(t, id, 0); if (max < 2) return 1;
    out[1] = hdl(t, id, 1); return 2;
}
int dm_six_handles(uint32_t *out, int max, uint32_t id, uint32_t style) {
    if (!out || max <= 0 || !npad_ok(id)) return 0;
    if (style == ST_FULLKEY || style == ST_HANDHELD) { out[0] = hdl(style == ST_FULLKEY ? 3 : 4, id, 2); return 1; }
    if (style == ST_JOYLEFT) { out[0] = hdl(6, id, 0); return 1; }
    if (style == ST_JOYRIGHT) { out[0] = hdl(7, id, 1); return 1; }
    if (style != ST_JOYDUAL) return 0;
    out[0] = hdl(5, id, 0); if (max < 2) return 1;
    out[1] = hdl(5, id, 1); return 2;
}
// The device's type and Npad of the controller connected now, as a handle's low 16 bits
static uint32_t connected_base(void) { uint32_t id = hle_hid_connected_npad(); return hdl(id == 0x20 ? 4 : 3, id, 0); }
static int connected(uint32_t h) { return (h & 0xffff) == connected_base(); }

// ---------- Vibration ----------
// nn::hid::VibrationValue {amplitudeLow, frequencyLow, amplitudeHigh, frequencyHigh}: amplitudes 0-1, frequencies in Hz (SDK reference; the
// defaults 160 / 320 Hz). A value holds until the next one. Values the game queues (EnqueueVibrationValue) take effect at FlushEnqueuedVibrationValues.
// The front end takes them at its own pace (dm_vibration_take), each band at the larger of the value now and the strongest since its last take,
// so a pulse shorter than that interval is still felt once (totk-mobile's peak hold; its captured Tears of the Kingdom landing pulse in the test).
// Off while the game does not permit vibration (PermitVibration), out of focus or in the background (dm_focus_now: what was playing stops; LASRecomp's
// rule) or while the front end says so (dm_vibration_active).
typedef struct { float al, fl, ah, fh; } vib_value;
_Static_assert(sizeof(vib_value) == 16, "VibrationValue");
static const vib_value k_silent = {0, 160, 0, 320};
static pthread_mutex_t g_vmu = PTHREAD_MUTEX_INITIALIZER;
static vib_value g_cur[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}}, g_pend[2], g_peak[2] = {{0, 160, 0, 320}, {0, 160, 0, 320}};
static unsigned g_pend_mask; static int g_vpermit = 1, g_vactive = 1, g_vfocus = 1;
int dm_focus_now(void);   // hle_oe.c: 1 in focus
static uint64_t g_vsent, g_vforwarded;
static vib_value vib_clean(vib_value v) {   // not finite or a frequency not above 0: silence; amplitudes into 0-1
    if (!isfinite(v.al) || !isfinite(v.fl) || !isfinite(v.ah) || !isfinite(v.fh) || v.fl <= 0 || v.fh <= 0) return k_silent;
    v.al = fminf(1, fmaxf(0, v.al)); v.ah = fminf(1, fmaxf(0, v.ah)); return v;
}
static void vib_commit(int side, vib_value v) {   // caller holds g_vmu
    g_cur[side] = v;
    if (v.al > g_peak[side].al) { g_peak[side].al = v.al; g_peak[side].fl = v.fl; }
    if (v.ah > g_peak[side].ah) { g_peak[side].ah = v.ah; g_peak[side].fh = v.fh; }
}
static void vib_clear(void) { g_cur[0] = g_cur[1] = g_peak[0] = g_peak[1] = k_silent; g_pend_mask = 0; }   // caller holds g_vmu
static void vib_put(uint32_t h, const vib_value *raw, int enqueue) {
    vib_value v = vib_clean(*raw); g_vsent++;
#ifndef DM_TEST_VIB_ALL_STYLES   // negative control of t_motion only: every style's values forwarded
    if (!connected(h)) return;   // another style's device: not connected, its zeros (or anything) go nowhere
#endif
    int side = (h >> 16 & 0xff) & 1;
    pthread_mutex_lock(&g_vmu);
    if (!g_vpermit || !g_vactive || dm_focus_now() != 1) v = k_silent;
    if (enqueue) { g_pend[side] = v; g_pend_mask |= 1u << side; }
    else vib_commit(side, v);
    g_vforwarded++;
    pthread_mutex_unlock(&g_vmu);
    static int nl; if (nl < 3 && (v.al > 0 || v.ah > 0)) { nl++; LOG("vibration: handle %#x low %.3f @ %.0f Hz, high %.3f @ %.0f Hz%s", h, v.al, v.fl, v.ah, v.fh, enqueue ? " (queued)" : ""); }
}
static void vib_flush(void) {
    pthread_mutex_lock(&g_vmu);
    for (int i = 0; i < 2; i++) if (g_pend_mask >> i & 1) vib_commit(i, g_pend[i]);
    g_pend_mask = 0; pthread_mutex_unlock(&g_vmu);
}
static vib_value vib_actual(uint32_t h) {
    if (!connected(h)) return k_silent;
    pthread_mutex_lock(&g_vmu); vib_value v = g_cur[(h >> 16 & 0xff) & 1]; pthread_mutex_unlock(&g_vmu); return v;
}
static void vib_permit(int on) { pthread_mutex_lock(&g_vmu); g_vpermit = on; if (!on) vib_clear(); pthread_mutex_unlock(&g_vmu); }
void dm_vibration_active(int on) {
    pthread_mutex_lock(&g_vmu); g_vactive = on != 0; vib_clear(); pthread_mutex_unlock(&g_vmu);
}
int dm_vibration_take(float out[2][4]) {
    int any = 0, focus = dm_focus_now() == 1;
    pthread_mutex_lock(&g_vmu);
    if (focus != g_vfocus) { g_vfocus = focus; vib_clear(); }   // into or out of focus: what was playing stops
    for (int i = 0; i < 2; i++) {
        vib_value v = g_cur[i];
        if (g_peak[i].al > v.al) { v.al = g_peak[i].al; v.fl = g_peak[i].fl; }
        if (g_peak[i].ah > v.ah) { v.ah = g_peak[i].ah; v.fh = g_peak[i].fh; }
#ifdef DM_TEST_VIB_NO_PEAK   // negative control of t_motion only: the value now, a pulse between two takes lost
        v = g_cur[i];
#endif
        g_peak[i] = k_silent;
        if (!g_vpermit || !g_vactive || !focus) v = k_silent;
        out[i][0] = v.al; out[i][1] = v.fl; out[i][2] = v.ah; out[i][3] = v.fh; any |= v.al > 0 || v.ah > 0;
    }
    pthread_mutex_unlock(&g_vmu);
    return any;
}
void dm_vibration_counts(uint64_t *sent, uint64_t *forwarded) { pthread_mutex_lock(&g_vmu); *sent = g_vsent; *forwarded = g_vforwarded; pthread_mutex_unlock(&g_vmu); }

HLE_FORCE("_ZN2nn3hid25GetVibrationDeviceHandlesEPNS0_21VibrationDeviceHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE",
          "vibration devices: the SDK's handles for the style", int, h_vib_handles, uint32_t *out, int max, const uint32_t *id, uint32_t style) {
    int n = id ? dm_vib_handles(out, max, *id, style) : 0;
    static int nl; if (nl < 4 && n) { nl++; LOG("vibration devices: Npad %#x style %#x → %d (%#x%s)", *id, style, n, out[0], connected(out[0]) ? ", connected" : ""); }
    return n;
}
HLE_FORCE("_ZN2nn3hid20IsVibrationPermittedEv", "vibration permitted: as PermitVibration left it (bool)", unsigned, h_vib_permitted, void) { return (unsigned)g_vpermit; }
HLE_FORCE("_ZN2nn3hid25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE", "vibration device: ready at once", void, h_vib_init, const uint32_t *h) { (void)h; }
HLE_FORCE("_ZN2nn3hid18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", "vibration: to the front end (the connected controller's)",
          void, h_vib_send, const uint32_t *h, const vib_value *v) { if (h && v) vib_put(*h, v, 0); }
HLE_FORCE_OPT("_ZN2nn3hid6detail19SendVibrationValuesEPKNS0_21VibrationDeviceHandleEPKNS0_14VibrationValueEi", "vibration (several): to the front end",
              Result, h_vib_send_many, const uint32_t *h, const vib_value *v, int n) { for (int i = 0; h && v && i < n; i++) vib_put(h[i], &v[i], 0); return RES_OK; }
HLE_FORCE_OPT("_ZN2nn3hid22GetVibrationDeviceInfoEPNS0_19VibrationDeviceInfoERKNS0_21VibrationDeviceHandleE", "vibration device info: LRA, left / right",
              void, h_vib_info, uint32_t *out, const uint32_t *h) { if (out && h) { out[0] = 1; out[1] = ((*h >> 16 & 0xff) & 1) + 1; } }
HLE_FORCE_OPT("_ZN2nn3hid23GetActualVibrationValueEPNS0_14VibrationValueERKNS0_21VibrationDeviceHandleE", "vibration: the value in effect",
              void, h_vib_actual, vib_value *out, const uint32_t *h) { if (out && h) *out = vib_actual(*h); }
HLE_FORCE_OPT("_ZN2nn3hid15PermitVibrationEb", "vibration permitted or not (bool: the low byte)", void, h_vib_permit, uint32_t on) { vib_permit((on & 0xff) != 0); }
// The detail functions (Tears of the Kingdom calls these: totk-mobile): the same, with a Result
HLE_FORCE_OPT("_ZN2nn3hid6detail20IsVibrationPermittedEPb", "vibration permitted (detail)", Result, h_vib_d_permitted, uint8_t *out) { if (out) *out = (uint8_t)g_vpermit; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn3hid6detail15PermitVibrationEb", "vibration permitted or not (detail; bool: the low byte)", Result, h_vib_d_permit, uint32_t on) { vib_permit((on & 0xff) != 0); return RES_OK; }
HLE_FORCE_OPT("_ZN2nn3hid6detail25InitializeVibrationDeviceERKNS0_21VibrationDeviceHandleE", "vibration device (detail): ready at once", Result, h_vib_d_init, const uint32_t *h) { (void)h; return RES_OK; }
HLE_FORCE_OPT("_ZN2nn3hid6detail22GetVibrationDeviceInfoEPNS0_19VibrationDeviceInfoERKNS0_21VibrationDeviceHandleE", "vibration device info (detail)",
              Result, h_vib_d_info, uint32_t *out, const uint32_t *h) { h_vib_info(out, h); return RES_OK; }
HLE_FORCE_OPT("_ZN2nn3hid6detail18SendVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", "vibration (detail): to the front end",
              Result, h_vib_d_send, const uint32_t *h, const vib_value *v) { if (h && v) vib_put(*h, v, 0); return RES_OK; }
HLE_FORCE_OPT("_ZN2nn3hid6detail21EnqueueVibrationValueERKNS0_21VibrationDeviceHandleERKNS0_14VibrationValueE", "vibration (detail): queued until the flush",
              Result, h_vib_d_enqueue, const uint32_t *h, const vib_value *v) { if (h && v) vib_put(*h, v, 1); return RES_OK; }
HLE_FORCE_OPT("_ZN2nn3hid6detail28FlushEnqueuedVibrationValuesEv", "vibration (detail): the queued values take effect", Result, h_vib_d_flush, void) { vib_flush(); return RES_OK; }
HLE_FORCE_OPT("_ZN2nn3hid6detail23GetActualVibrationValueEPNS0_14VibrationValueERKNS0_21VibrationDeviceHandleE", "vibration (detail): the value in effect",
              Result, h_vib_d_actual, vib_value *out, const uint32_t *h) { if (out && h) *out = vib_actual(*h); return RES_OK; }
HLE_FORCE_OPT("_ZN2nn3hid6detail24IsVibrationDeviceMountedEPbRKNS0_21VibrationDeviceHandleE", "vibration device mounted: the connected controller's",
              Result, h_vib_d_mounted, uint8_t *out, const uint32_t *h) { if (out && h) *out = (uint8_t)connected(*h); return RES_OK; }

// ---------- Six-axis sensor ----------
// nn::hid::SixAxisSensorState, 0x60 bytes: {deltaTime ns, samplingNumber, acceleration (G), angularVelocity (360 dps = 1), angle (360 degrees = 1,
// the angular velocity integrated), direction (3x3), attributes (bit 0 connected), reserved} (SDK reference for the members and units; the
// direction and its offset 0x34 as atone/nxnative's and SkyRecomp's, both MIT). The front end gives acceleration and angular velocity in nn::hid's
// frame (dm_motion_set); the orientation is kept by sensor fusion as atone/nxnative 185cfda keeps it (Mahony's complementary filter, the gyro
// turning a quaternion and the accelerometer pulling its gravity estimate back so it stays level and does not drift; handed out as three axis
// vectors in the component order and signs games were written against; their method, checked by them with Breath of the Wild's bow aiming on a
// Pro Controller; written anew here). The gyro's zero point is learned while the controller is still (the SDK documents that its library does
// so continuously, by GyroscopeZeroDriftMode: Loose, Standard (the default), Tight; BotWRecomp's averaging of the still samples, the idea).
// StartSixAxisSensor sets the reference pose (SDK: its Z axis the gravity's, X / Y as the controller is held then): the next sample starts the
// filter level. Without a source no state is connected: GetSixAxisSensorStates gives none, GetSixAxisSensorState a zeroed one (it wrote nothing
// before, leaving the caller's stack as it was).
typedef struct { int64_t dt_ns, sampling; float acc[3], gyro[3], angle[3], dir[3][3]; uint32_t attributes, _rsv; } six_state;
_Static_assert(sizeof(six_state) == 0x60, "SixAxisSensorState");
_Static_assert(__builtin_offsetof(six_state, dir) == 0x34 && __builtin_offsetof(six_state, attributes) == 0x58, "SixAxisSensorState layout");
enum { REST_SAMPLES = 200 };   // still samples before the zero point is taken from their mean
static pthread_mutex_t g_smu = PTHREAD_MUTEX_INITIALIZER;
static struct {
    six_state s; uint64_t last_ns; int have, relevel, rest, drift, fusion; float revise_power, revise_range;
    double q[4];   // (x, y, z, w), level when (0, 0, -1, 0)
    double err[3], ierr[3];
    double bias[3], bsum[3]; int bn, bias_set;
} g_six = {.s = {.dir = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, .q = {0, 0, -1, 0}, .drift = 1, .fusion = 1};
static double n3(const double *v) { return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); }
// One filter step: w = angular velocity (rad/s) in the filter's axes, a = the normalized acceleration (NULL: none this step)
static void six_step(double w[3], const double *a, double gain, double dt) {
    double *q = g_six.q, x = q[0], y = q[1], z = q[2], qw = q[3];
    if (a) {
        double ax = -a[0], ay = a[1], az = -a[2];
        double v[3] = {2 * (x * z - qw * y), 2 * (qw * x + y * z), qw * qw - x * x - y * y + z * z};   // gravity as the quaternion has it
        double e[3] = {az * v[0] - ax * v[2], ay * v[2] - az * v[1], ax * v[1] - ay * v[0]};             // measured against estimated
        int big = n3(e) >= 0.05;
        for (int i = 0; i < 3; i++) { g_six.err[i] = e[i]; g_six.ierr[i] = big ? g_six.ierr[i] + e[i] : 0; w[i] += gain * (0.3 * e[i] + 0.005 * g_six.ierr[i]); }
    }
    double gx = w[1], gy = w[0], gz = w[2], h = 0.5 * dt;
    double nw = qw + (-x * gx - y * gy - z * gz) * h;
    double nx = x + (nw * gx + y * gz - z * gy) * h, ny = y + (nw * gy - x * gz + z * gx) * h, nz = z + (nw * gz + x * gy - y * gx) * h;
    double m = sqrt(nx * nx + ny * ny + nz * nz + nw * nw);
    if (m > 0) { q[0] = nx / m; q[1] = ny / m; q[2] = nz / m; q[3] = nw / m; }
}
static void six_dir(float d[3][3]) {   // the axis vectors, in the order and signs games read
    const double *q = g_six.q; double x = -q[1], y = -q[0], z = -q[3], w = -q[2];
    double m[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y),
                   2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x),
                   2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)};
    d[0][0] = (float)m[0];  d[0][1] = (float)m[1];  d[0][2] = (float)-m[2];
    d[1][0] = (float)m[3];  d[1][1] = (float)m[4];  d[1][2] = (float)-m[5];
    d[2][0] = (float)-m[6]; d[2][1] = (float)-m[7]; d[2][2] = (float)m[8];
}
static void six_level(const double *na) {   // the filter to the accelerometer's level at once (yaw as it is)
    g_six.q[0] = g_six.q[1] = g_six.q[3] = 0; g_six.q[2] = -1;
    memset(g_six.ierr, 0, sizeof g_six.ierr);
    for (int i = 0; i < 200 && na; i++) { double w0[3] = {0}; six_step(w0, na, 10, 0.015); if (n3(g_six.err) < 0.002) break; }
}
static const double k_rest_gyro[3] = {0.05, 0.025, 0.012};   // rev/s off the zero point (18, 9, 4.3 degrees a second): still below this (Loose, Standard, Tight)
void dm_motion_set(const float acc_in[3], const float gyro_in[3], uint64_t t_ns) {
    pthread_mutex_lock(&g_smu);
    int64_t dt = g_six.last_ns && t_ns > g_six.last_ns ? (int64_t)(t_ns - g_six.last_ns) : 0; if (dt > 100000000) dt = 0;   // the first sample or a gap: no step
    g_six.last_ns = t_ns;
    double a[3], g[3], s = dt / 1e9;
    for (int i = 0; i < 3; i++) { a[i] = fmax(-8, fmin(8, acc_in[i])); g[i] = fmax(-6, fmin(6, gyro_in[i])); }
    double an = n3(a), na[3] = {an > 0 ? a[0] / an : 0, an > 0 ? a[1] / an : 0, an > 0 ? a[2] / an : 0};
    int drift = g_six.drift >= 0 && g_six.drift <= 2 ? g_six.drift : 1;
    double off[3] = {g[0] - g_six.bias[0], g[1] - g_six.bias[1], g[2] - g_six.bias[2]};
    int still = n3(off) < k_rest_gyro[drift] && an > 0.95 && an < 1.05;
#ifndef DM_TEST_SIX_NO_BIAS   // negative control of t_motion only: the zero point never learned
    if (still) {
        for (int i = 0; i < 3; i++) g_six.bsum[i] += g[i];
        if (++g_six.bn >= REST_SAMPLES) { for (int i = 0; i < 3; i++) g_six.bias[i] = g_six.bsum[i] / g_six.bn; g_six.bias_set = 1;
                                          if (g_six.bn >= 4 * REST_SAMPLES) { for (int i = 0; i < 3; i++) g_six.bsum[i] = g_six.bias[i] * REST_SAMPLES; g_six.bn = REST_SAMPLES; } }
    }
#endif
    for (int i = 0; i < 3; i++) g[i] -= g_six.bias[i];
    if (n3(g) < 0.007) g[0] = g[1] = g[2] = 0;   // below the noise: no turn
    g_six.rest = still;
    if (g_six.relevel || !g_six.have) { six_level(an > 0.5 ? na : NULL); g_six.relevel = 0; }
    else if (s > 0) {
        double w[3] = {g[1] * 2 * M_PI, -g[0] * 2 * M_PI, -g[2] * 2 * M_PI};   // the filter's axes
#ifndef DM_TEST_SIX_NO_GRAVITY   // negative control of t_motion only: the gyro alone
        six_step(w, g_six.fusion && an >= 0.75 && an <= 1.25 ? na : NULL, 1, s);   // no correction while the acceleration is not mostly gravity
#else
        six_step(w, NULL, 1, s);
#endif
    }
    six_state *o = &g_six.s;
    o->dt_ns = dt ? dt : 5000000; o->sampling++;
    for (int i = 0; i < 3; i++) { o->acc[i] = (float)a[i]; o->gyro[i] = (float)g[i]; o->angle[i] += (float)(g[i] * s); }
    six_dir(o->dir); o->attributes = 1; g_six.have = 1;
    pthread_mutex_unlock(&g_smu);
    static int nl; if (!nl++) LOG("six-axis: the front end's motion arrives (acceleration %.2f %.2f %.2f G)", a[0], a[1], a[2]);
}
void dm_motion_lost(void) {   // the source went away: not connected until it comes back (the zero point kept)
    pthread_mutex_lock(&g_smu); g_six.have = 0; g_six.last_ns = 0; g_six.s.attributes = 0; pthread_mutex_unlock(&g_smu);
}
static int six_get(six_state *o) { pthread_mutex_lock(&g_smu); *o = g_six.s; int h = g_six.have; pthread_mutex_unlock(&g_smu); return h; }

HLE_FORCE("_ZN2nn3hid23GetSixAxisSensorHandlesEPNS0_19SixAxisSensorHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE",
          "six-axis sensors: the SDK's handles for the style", int, h_six_handles, uint32_t *out, int max, const uint32_t *id, uint32_t style) {
    int n = id ? dm_six_handles(out, max, *id, style) : 0;
    static int nl; if (nl < 4 && n) { nl++; LOG("six-axis sensors: Npad %#x style %#x → %d (%#x%s)", *id, style, n, out[0], connected(out[0]) ? ", connected" : ""); }
    return n;
}
HLE_FORCE_OPT("_ZN2nn3hid18StartSixAxisSensorERKNS0_19SixAxisSensorHandleE", "six-axis sampling started: the reference pose taken at the next sample",
              void, h_six_start, const uint32_t *h) { if (h && connected(*h)) { pthread_mutex_lock(&g_smu); g_six.relevel = 1; pthread_mutex_unlock(&g_smu); } }
HLE_FORCE_OPT("_ZN2nn3hid17StopSixAxisSensorERKNS0_19SixAxisSensorHandleE", "six-axis sampling stopped: the source goes on", void, h_six_stop, const uint32_t *h) { (void)h; }
HLE_FORCE_OPT("_ZN2nn3hid21GetSixAxisSensorStateEPNS0_18SixAxisSensorStateERKNS0_19SixAxisSensorHandleE", "six-axis state: the front end's motion, or not connected",
              void, h_six_state, six_state *out, const uint32_t *h) {
    if (!out) return;
    six_state s; int have = six_get(&s);
    if (!have || !h || !connected(*h)) { int64_t n = s.sampling; memset(out, 0, sizeof *out); out->sampling = n; return; }
    *out = s;
}
// Newest first: the current sample with consecutive sampling numbers going back (as GetNpadStates); none without a source
HLE_FORCE_OPT("_ZN2nn3hid22GetSixAxisSensorStatesEPNS0_18SixAxisSensorStateEiRKNS0_19SixAxisSensorHandleE", "six-axis states: the front end's motion, newest first",
              int, h_six_states, six_state *out, int n, const uint32_t *h) {
    six_state s; if (!out || n <= 0 || !h || !connected(*h) || !six_get(&s)) return 0;
    for (int i = 0; i < n; i++) { out[i] = s; out[i].sampling = s.sampling - i; }
    return n;
}
HLE_FORCE_OPT("_ZN2nn3hid21IsSixAxisSensorAtRestERKNS0_19SixAxisSensorHandleE", "six-axis at rest: still by the zero point's rule (no source: yes)",
              unsigned, h_six_rest, const uint32_t *h) { (void)h; pthread_mutex_lock(&g_smu); unsigned r = !g_six.have || g_six.rest; pthread_mutex_unlock(&g_smu); return r; }
HLE_FORCE_OPT("_ZN2nn3hid25EnableSixAxisSensorFusionERKNS0_19SixAxisSensorHandleEb", "six-axis fusion on / off (bool: the low byte)",
              void, h_six_fusion_on, const uint32_t *h, uint32_t on) { (void)h; pthread_mutex_lock(&g_smu); g_six.fusion = (on & 0xff) != 0; pthread_mutex_unlock(&g_smu); }
HLE_FORCE_OPT("_ZN2nn3hid28IsSixAxisSensorFusionEnabledERKNS0_19SixAxisSensorHandleE", "six-axis fusion: as set (on at start)",
              unsigned, h_six_fusion_q, const uint32_t *h) { (void)h; return (unsigned)g_six.fusion; }
HLE_FORCE_OPT("_ZN2nn3hid32SetSixAxisSensorFusionParametersERKNS0_19SixAxisSensorHandleEff", "six-axis fusion parameters: kept", void, h_six_fpar_set, const uint32_t *h, float p, float r) {
    (void)h; pthread_mutex_lock(&g_smu); g_six.revise_power = p; g_six.revise_range = r; pthread_mutex_unlock(&g_smu); }
HLE_FORCE_OPT("_ZN2nn3hid32GetSixAxisSensorFusionParametersEPfS1_RKNS0_19SixAxisSensorHandleE", "six-axis fusion parameters: as set", void, h_six_fpar_get, float *p, float *r, const uint32_t *h) {
    (void)h; pthread_mutex_lock(&g_smu); if (p) *p = g_six.revise_power; if (r) *r = g_six.revise_range; pthread_mutex_unlock(&g_smu); }
HLE_FORCE_OPT("_ZN2nn3hid34ResetSixAxisSensorFusionParametersERKNS0_19SixAxisSensorHandleE", "six-axis fusion parameters: back to 0", void, h_six_fpar_reset, const uint32_t *h) {
    (void)h; pthread_mutex_lock(&g_smu); g_six.revise_power = g_six.revise_range = 0; pthread_mutex_unlock(&g_smu); }
HLE_FORCE_OPT("_ZN2nn3hid25GetGyroscopeZeroDriftModeERKNS0_19SixAxisSensorHandleE", "gyroscope zero drift mode: as set (Standard at start)", int, h_six_drift_get, const uint32_t *h) { (void)h; return g_six.drift; }
HLE_FORCE_OPT("_ZN2nn3hid25SetGyroscopeZeroDriftModeERKNS0_19SixAxisSensorHandleERKNS0_22GyroscopeZeroDriftModeE", "gyroscope zero drift mode: kept (its still threshold)",
              void, h_six_drift_set, const uint32_t *h, const int *m) { (void)h; if (m && *m >= 0 && *m <= 2) g_six.drift = *m; }
HLE_FORCE_OPT("_ZN2nn3hid41IsFirmwareUpdateAvailableForSixAxisSensorERKNS0_19SixAxisSensorHandleE", "six-axis firmware update: none", unsigned, h_six_fw, const uint32_t *h) { (void)h; return 0; }
