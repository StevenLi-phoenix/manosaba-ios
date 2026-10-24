// The six-axis sensor and the vibration devices of the connected controller (host/hle/hle_motion.c, plan item 67):
//   handles as Tears of the Kingdom's sdk makes them (vibration: full key {3, 0x10003}, handheld 0x20 {0x2004, 0x12004}, Joy-Con pair, left, right;
//   six-axis: full key {0x20003}, handheld {0x22004}, pair two, left, right; another style or Npad none)
//   vibration: only the connected controller's devices reach the front end (docked: full key; a handheld device's values go nowhere); queued values
//   at the flush; a pulse between two takes felt once, then silence; totk-mobile's captured Tears of the Kingdom landing pulse (16 steps, Apache-2.0,
//   the data) one take each, no tail; the landing value {0.1, 198.7 Hz, 0, 299.9 Hz} as it is; not finite or a frequency at 0: silence, amplitudes
//   clamped; PermitVibration's bool from its low byte (0x100 off, 0x101 on); inactive or out of focus silent; device info LRA left / right; mounted
//   only the connected controller's
//   six-axis: no state before the front end's motion (GetSixAxisSensorState zeroed, not left as it was); flat and still: connected, newest first with
//   consecutive sampling numbers, the direction the identity; a quarter turn about the vertical: angle 0.25, the X axis turned a quarter, still level;
//   rolled 30 degrees and still: the gravity pulls the direction's Z axis to 30 degrees; a constant gyro offset while still: learned, the angle stops
//   growing; at rest while still, not while turning; the fusion switch's bool from its low byte; the zero drift mode kept; the source lost: no state;
//   handheld: the handheld sensor connected, the full key one not
// Every style's values forwarded (-DDM_TEST_VIB_ALL_STYLES): 1 fails; no peak hold (-DDM_TEST_VIB_NO_PEAK): 1 fails; the zero point never learned
// (-DDM_TEST_SIX_NO_BIAS): 1 fails; the gyro alone, no gravity (-DDM_TEST_SIX_NO_GRAVITY): 1 fails.
#include "../hle/hle_hid.c"
#include "../hle/hle_motion.c"
module_t g_mod[MAX_MOD]; int g_nmod;
void hle_bind_system_event(void *g, int autoclear) { (void)g; (void)autoclear; }
uint64_t now_ns(void) { return 1000000000ull; }
uint64_t guest_ns(void) { return 1000000000ull; }
uint64_t dm_nvn_frame_count(void) { return 0; }
static int opmode = 1, focus = 1;
int dm_opmode(void) { return opmode; }
int dm_focus_now(void) { return focus; }
int touch_fill(void *p, uint64_t n) { (void)p; (void)n; return 0; }
void dm_touch_set(const dm_touch *p, int n) { (void)p; (void)n; }
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-110s %s\n", what, ok ? "✓" : "✗"); }
static int near(float a, float b, float tol) { return fabsf(a - b) <= tol; }
static uint64_t t_ns = 5000000000ull;
static void feed(float ax, float ay, float az, float gx, float gy, float gz, int k) {   // k samples 5 ms apart
    for (int i = 0; i < k; i++) { float a[3] = {ax, ay, az}, g[3] = {gx, gy, gz}; t_ns += 5000000; dm_motion_set(a, g, t_ns); }
}
static int same4(const float v[4], float a, float b, float c, float d) { return v[0] == a && v[1] == b && v[2] == c && v[3] == d; }
int main(void) {
    uint32_t h[4], id; int k; char line[200];
    // ---- handles
    id = 0; k = h_vib_handles(h, 4, &id, ST_FULLKEY); check("vibration, full key, Npad 0: {3, 0x10003}", k == 2 && h[0] == 3 && h[1] == 0x10003);
    id = 0x20; k = h_vib_handles(h, 4, &id, ST_HANDHELD); check("vibration, handheld: {0x2004, 0x12004}", k == 2 && h[0] == 0x2004 && h[1] == 0x12004);
    id = 1; k = h_vib_handles(h, 4, &id, ST_JOYDUAL); check("vibration, Joy-Con pair, Npad 1: {0x105, 0x10105}", k == 2 && h[0] == 0x105 && h[1] == 0x10105);
    id = 0; k = h_vib_handles(h, 4, &id, ST_JOYLEFT); int kl = k; uint32_t hl = h[0]; k = h_vib_handles(h, 4, &id, ST_JOYRIGHT);
    check("vibration, single Joy-Cons: left {6}, right {0x10007}", kl == 1 && hl == 6 && k == 1 && h[0] == 0x10007);
    id = 0; k = h_vib_handles(h, 1, &id, ST_FULLKEY); int k1 = k; k = h_vib_handles(h, 4, &id, 1u << 5); int k2 = k; id = 9; k = h_vib_handles(h, 4, &id, ST_FULLKEY);
    check("vibration: room for one gives one; another style none; Npad 9 none", k1 == 1 && k2 == 0 && k == 0);
    id = 0; k = h_six_handles(h, 4, &id, ST_FULLKEY); int s1 = k == 1 && h[0] == 0x20003; id = 0x20; k = h_six_handles(h, 4, &id, ST_HANDHELD); int s2 = k == 1 && h[0] == 0x22004;
    id = 0; k = h_six_handles(h, 4, &id, ST_JOYDUAL); int s3 = k == 2 && h[0] == 5 && h[1] == 0x10005;
    check("six-axis: full key {0x20003}, handheld {0x22004}, pair {5, 0x10005}", s1 && s2 && s3);
    // ---- vibration
    float out[2][4]; vib_value v = {0.2f, 160, 0.5f, 320}, z = {0, 160, 0, 320}, hv = {0.7f, 160, 0.7f, 320};
    uint32_t L = 3, R = 0x10003, HH = 0x2004;
    opmode = 1; h_vib_send(&L, &v); h_vib_send(&HH, &hv); dm_vibration_take(out); vib_value a = {0}; h_vib_actual(&a, &HH);
    check("docked: the full key device's value reaches the front end, the handheld one's not (its actual value silent)", near(out[0][0], 0.2f, 0) && near(out[0][2], 0.5f, 0) && a.al == 0 && a.ah == 0);
    h_vib_send(&L, &z); dm_vibration_take(out);
    h_vib_d_enqueue(&R, &v); dm_vibration_take(out); int before = out[1][0] == 0; h_vib_d_flush(); dm_vibration_take(out);
    check("a queued value at the flush, not before", before && near(out[1][0], 0.2f, 0));
    h_vib_send(&R, &z); dm_vibration_take(out);
    vib_value p = {0, 160, 0.3f, 320}; h_vib_send(&L, &p); h_vib_send(&L, &z); dm_vibration_take(out); float got = out[0][2]; dm_vibration_take(out);
    check("a pulse between two takes: felt once (high 0.3), then silence", near(got, 0.3f, 0) && out[0][2] == 0);
    static const float captured[] = {.071373f, .1f, .1f, .1f, .1f, .099216f, .041176f, .059608f, .045882f, .015686f, .011373f, .009020f, .002353f, .002745f, .000392f, 0};
    int ok = 1; uint32_t both[2] = {3, 0x10003};
    for (unsigned i = 0; i < sizeof captured / sizeof *captured; i++) {
        vib_value pv[2] = {{captured[i], 160, 0, 320}, {captured[i], 160, 0, 320}}; h_vib_send_many(both, pv, 2); dm_vibration_take(out);
        ok &= out[0][0] == captured[i] && out[1][0] == captured[i];
    }
    dm_vibration_take(out); ok &= out[0][0] == 0 && out[1][0] == 0;
    check("totk-mobile's captured landing pulse (16 steps): each step as sent, no tail", ok);
    vib_value land = {0.1f, 198.7f, 0, 299.9f}; h_vib_d_send(&L, &land); dm_vibration_take(out);
    check("the landing value {0.1, 198.7 Hz, 0, 299.9 Hz} as it is", same4(out[0], 0.1f, 198.7f, 0, 299.9f));
    vib_value bad = {NAN, 160, 1, 320}; h_vib_send(&L, &bad); dm_vibration_take(out); int nan_ok = out[0][0] == 0 && out[0][2] == 0;
    vib_value wide = {-2, 160, 9, 320}; h_vib_send(&L, &wide); dm_vibration_take(out); int cl_ok = out[0][0] == 0 && out[0][2] == 1;
    vib_value f0 = {0.5f, 0, 0.5f, 320}; h_vib_send(&L, &f0); dm_vibration_take(out); int f_ok = out[0][0] == 0 && out[0][2] == 0;
    check("not finite: silence; amplitudes -2 / 9 clamped to 0 / 1; a frequency at 0: silence", nan_ok && cl_ok && f_ok);
    h_vib_d_permit(0x100); uint8_t pm = 9; h_vib_d_permitted(&pm); h_vib_send(&L, &v); dm_vibration_take(out); int off = out[0][0] == 0 && pm == 0 && h_vib_permitted() == 0;
    h_vib_permit(0x101); h_vib_send(&L, &v); dm_vibration_take(out);
    check("PermitVibration(0x100): off (the low byte), silent; (0x101): on again", off && near(out[0][0], 0.2f, 0) && h_vib_permitted() == 1);
    dm_vibration_active(0); h_vib_send(&L, &v); int was = dm_vibration_take(out); dm_vibration_active(1); int after = dm_vibration_take(out);
    check("inactive (the front end): silent; active again: silent until the next value", !was && out[0][0] == 0 && !after);
    h_vib_send(&L, &v); focus = 2; int of1 = dm_vibration_take(out); h_vib_send(&L, &v); int of2 = dm_vibration_take(out); focus = 1; int of3 = dm_vibration_take(out);
    check("out of focus: what was playing stops, new values silent; in focus again: silent until the next value", !of1 && !of2 && !of3);
    uint32_t info[2], RJ = 0x10007; h_vib_info(info, &L); int i1 = info[0] == 1 && info[1] == 1; h_vib_info(info, &R); int i2 = info[1] == 2; h_vib_info(info, &RJ);
    uint8_t m1 = 0, m2 = 1; h_vib_d_mounted(&m1, &L); h_vib_d_mounted(&m2, &HH);
    check("device info: LRA, left (1) / right (2), the right Joy-Con right; mounted: the connected controller's only", i1 && i2 && info[1] == 2 && m1 == 1 && m2 == 0);
    // ---- six-axis
    uint32_t SF = 0x20003, SH = 0x22004; six_state st[4]; memset(st, 0xa5, sizeof st);
    int none = h_six_states(st, 4, &SF); h_six_state(&st[0], &SF);
    check("before any motion: no states; the single state zeroed (not connected), not left as it was", none == 0 && st[0].attributes == 0 && st[0].acc[0] == 0 && st[0].dir[0][0] == 0);
    h_six_start(&SF); feed(0, 0, -1, 0, 0, 0, 200);
    k = h_six_states(st, 3, &SF);
    snprintf(line, sizeof line, "flat, still: 3 states, connected, sampling %lld %lld %lld, direction the identity", (long long)st[0].sampling, (long long)st[1].sampling, (long long)st[2].sampling);
    check(line, k == 3 && st[0].attributes == 1 && st[1].sampling == st[0].sampling - 1 && st[2].sampling == st[0].sampling - 2 &&
          near(st[0].dir[0][0], 1, 1e-3f) && near(st[0].dir[1][1], 1, 1e-3f) && near(st[0].dir[2][2], 1, 1e-3f));
    check("still: at rest", h_six_rest(&SF) == 1);
    float a0 = st[0].angle[2]; feed(0, 0, -1, 0, 0, 1, 50); h_six_state(&st[0], &SF);
    int rest_turning = h_six_rest(&SF);
    snprintf(line, sizeof line, "a quarter turn about the vertical: angle %.3f, X axis (%.2f %.2f %.2f), Z axis (%.2f %.2f %.2f); not at rest", st[0].angle[2] - a0,
             st[0].dir[0][0], st[0].dir[0][1], st[0].dir[0][2], st[0].dir[2][0], st[0].dir[2][1], st[0].dir[2][2]);
    check(line, near(st[0].angle[2] - a0, 0.25f, 0.01f) && fabsf(st[0].dir[0][0]) < 0.05f && fabsf(st[0].dir[0][1]) > 0.99f && near(st[0].dir[2][2], 1, 0.01f) && !rest_turning);
    h_six_start(&SF); feed(0, 0, -1, 0, 0, 0, 10);
    float c30 = cosf((float)M_PI / 6), s30 = sinf((float)M_PI / 6);
    feed(0, -s30, -c30, 0, 0, 0, 600); h_six_state(&st[0], &SF);
    snprintf(line, sizeof line, "rolled 30 degrees, still for 3 s: the direction's Z axis to 30 degrees (z %.3f, cos 30 = %.3f)", st[0].dir[2][2], c30);
    check(line, near(st[0].dir[2][2], c30, 0.03f));
    h_six_start(&SF); feed(0, 0, -1, 0, 0, 0.01f, 600); h_six_state(&st[0], &SF); float g0 = st[0].angle[2]; feed(0, 0, -1, 0, 0, 0.01f, 100); h_six_state(&st[1], &SF);
    snprintf(line, sizeof line, "a constant 0.01 rev/s gyro offset while still: learned (angular velocity %.4f, angle %.5f more in 0.5 s)", st[1].gyro[2], st[1].angle[2] - g0);
    check(line, fabsf(st[1].gyro[2]) < 0.001f && fabsf(st[1].angle[2] - g0) < 0.001f);
    h_six_fusion_on(&SF, 0x100); int fo = h_six_fusion_q(&SF); h_six_fusion_on(&SF, 0x101);
    int tight = 2; h_six_drift_set(&SF, &tight);
    check("fusion (0x100): off by the low byte, (0x101) on; zero drift mode Tight kept", fo == 0 && h_six_fusion_q(&SF) == 1 && h_six_drift_get(&SF) == 2);
    dm_motion_lost(); k = h_six_states(st, 2, &SF);
    check("the source lost: no states", k == 0);
    opmode = 0; feed(0, 0, -1, 0, 0, 0, 5); int kh = h_six_states(st, 1, &SH), kf = h_six_states(st, 1, &SF);
    check("handheld: the handheld sensor's states, none for the full key one", kh == 1 && kf == 0);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
