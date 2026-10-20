// A Switch Pro Controller's reports (host/procon.h, plan item 67b): a report 0x30's IMU in nn::hid's frame (flat: acceleration (0, 0, -1); the
// controller's raw forward axis to nn::hid's -y, its left to x; the gyro about raw x / y / z to (0, 1, 0) / (-1, 0, 0) / (0, 0, 1) revolutions a
// second at 5,898 LSB), the three samples averaged, a short report or another ID refused; the HD rumble amplitude code of every row of dekuNukem's
// amplitude table (HA byte / 2) and 100 above it; a motor's four bytes: none {00 01 40 40}, both bands at the table's 1.0029 {00 c9 40 72}, the low
// band 0.5 at 160 Hz {00 01 40 62} (its table row's LA bytes 00 62). The raw axes as they come (-DDM_TEST_PROCON_RAW_AXES): 3 fail.
#include "../procon.h"
#include <stdio.h>
#include <string.h>
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
static int near(float a, float b, float t) { return fabsf(a - b) <= t; }
static void put16(uint8_t *p, int16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((uint16_t)v >> 8); }
static void report(uint8_t *r, const int16_t acc[3][3], const int16_t gyro[3][3]) {   // three samples
    memset(r, 0, 64); r[0] = 0x30;
    for (int f = 0; f < 3; f++) for (int i = 0; i < 3; i++) { put16(r + 13 + 12 * f + 2 * i, acc[f][i]); put16(r + 19 + 12 * f + 2 * i, gyro[f][i]); }
}
// dekuNukem's amplitude table: {HA byte, amplitude}
static const struct { int ha; float a; } k_amp[] = {
    {0x00, 0.000000f}, {0x02, 0.007843f}, {0x04, 0.011823f}, {0x06, 0.014061f}, {0x08, 0.016720f}, {0x0a, 0.019885f}, {0x0c, 0.023648f}, {0x0e, 0.028123f},
    {0x10, 0.033442f}, {0x12, 0.039771f}, {0x14, 0.047296f}, {0x16, 0.056246f}, {0x18, 0.066886f}, {0x1a, 0.079542f}, {0x1c, 0.094592f}, {0x1e, 0.112491f},
    {0x20, 0.117471f}, {0x22, 0.122671f}, {0x24, 0.128102f}, {0x26, 0.133774f}, {0x28, 0.139697f}, {0x2a, 0.145882f}, {0x2c, 0.152341f}, {0x2e, 0.159085f},
    {0x30, 0.166129f}, {0x32, 0.173484f}, {0x34, 0.181166f}, {0x36, 0.189185f}, {0x38, 0.197561f}, {0x3a, 0.206308f}, {0x3c, 0.215442f}, {0x3e, 0.224982f},
    {0x40, 0.229908f}, {0x42, 0.234943f}, {0x44, 0.240087f}, {0x46, 0.245345f}, {0x48, 0.250715f}, {0x4a, 0.256206f}, {0x4c, 0.261816f}, {0x4e, 0.267549f},
    {0x50, 0.273407f}, {0x52, 0.279394f}, {0x54, 0.285514f}, {0x56, 0.291765f}, {0x58, 0.298154f}, {0x5a, 0.304681f}, {0x5c, 0.311353f}, {0x5e, 0.318171f},
    {0x60, 0.325138f}, {0x62, 0.332258f}, {0x64, 0.339534f}, {0x66, 0.346969f}, {0x68, 0.354566f}, {0x6a, 0.362331f}, {0x6c, 0.370265f}, {0x6e, 0.378372f},
    {0x70, 0.386657f}, {0x72, 0.395124f}, {0x74, 0.403777f}, {0x76, 0.412619f}, {0x78, 0.421652f}, {0x7a, 0.430885f}, {0x7c, 0.440321f}, {0x7e, 0.449964f},
    {0x80, 0.459817f}, {0x82, 0.469885f}, {0x84, 0.480174f}, {0x86, 0.490689f}, {0x88, 0.501433f}, {0x8a, 0.512413f}, {0x8c, 0.523633f}, {0x8e, 0.535100f},
    {0x90, 0.546816f}, {0x92, 0.558790f}, {0x94, 0.571027f}, {0x96, 0.583530f}, {0x98, 0.596307f}, {0x9a, 0.609365f}, {0x9c, 0.622708f}, {0x9e, 0.636344f},
    {0xa0, 0.650279f}, {0xa2, 0.664518f}, {0xa4, 0.679069f}, {0xa6, 0.693939f}, {0xa8, 0.709133f}, {0xaa, 0.724662f}, {0xac, 0.740529f}, {0xae, 0.756745f},
    {0xb0, 0.773316f}, {0xb2, 0.790249f}, {0xb4, 0.807554f}, {0xb6, 0.825237f}, {0xb8, 0.843307f}, {0xba, 0.861772f}, {0xbc, 0.880643f}, {0xbe, 0.899928f},
    {0xc0, 0.919633f}, {0xc2, 0.939771f}, {0xc4, 0.960348f}, {0xc6, 0.981378f}, {0xc8, 1.002867f}};
int main(void) {
    uint8_t r[64]; float a[3], g[3];
    int16_t flat[3][3] = {{0, 0, 4000}, {0, 0, 4096}, {0, 0, 4192}}, still[3][3] = {{0}};
    report(r, flat, still);
    check("flat (raw z +1 G, three samples 4000 / 4096 / 4192): acceleration (0, 0, -1), gyro 0", procon_imu(r, 49, a, g) && near(a[0], 0, 1e-6f) && near(a[1], 0, 1e-6f) && near(a[2], -1, 1e-6f) && g[0] == 0 && g[2] == 0);
    int16_t fwd[3][3] = {{4096, 0, 0}, {4096, 0, 0}, {4096, 0, 0}}, left[3][3] = {{0, 4096, 0}, {0, 4096, 0}, {0, 4096, 0}};
    report(r, fwd, still); procon_imu(r, 49, a, g); int f1 = near(a[0], 0, 1e-6f) && near(a[1], -1, 1e-6f) && near(a[2], 0, 1e-6f);
    report(r, left, still); procon_imu(r, 49, a, g);
    check("raw forward +1 G → nn::hid y -1; raw left +1 G → x +1", f1 && near(a[0], 1, 1e-6f) && near(a[1], 0, 1e-6f));
    int16_t gx[3][3] = {{5898, 0, 0}, {5898, 0, 0}, {5898, 0, 0}}, gy[3][3] = {{0, 5898, 0}, {0, 5898, 0}, {0, 5898, 0}}, gz[3][3] = {{0, 0, 5898}, {0, 0, 5898}, {0, 0, 5898}};
    float rx[3], ry[3], rz[3];
    report(r, flat, gx); procon_imu(r, 49, a, rx); report(r, flat, gy); procon_imu(r, 49, a, ry); report(r, flat, gz); procon_imu(r, 49, a, rz);
    check("gyro 5,898 LSB about raw x / y / z → (0, 1, 0) / (-1, 0, 0) / (0, 0, 1) revolutions a second", near(rx[1], 1, 1e-3f) && near(rx[0], 0, 1e-6f) &&
          near(ry[0], -1, 1e-3f) && near(ry[1], 0, 1e-6f) && near(rz[2], 1, 1e-3f));
    report(r, flat, still); int bad1 = procon_imu(r, 48, a, g); r[0] = 0x21; int bad2 = procon_imu(r, 49, a, g);
    check("a report under 49 bytes, or another ID (0x21): refused", !bad1 && !bad2);
    int all = 1; char miss[128] = "";
    for (unsigned i = 0; i < sizeof k_amp / sizeof *k_amp; i++) { int c = procon_amp_code(k_amp[i].a); if (c != k_amp[i].ha / 2) { all = 0; snprintf(miss, sizeof miss, " (%.6f → %d, the table %d)", k_amp[i].a, c, k_amp[i].ha / 2); } }
    char line[200]; snprintf(line, sizeof line, "the amplitude code of all %zu rows of the table%s; 1.5 → 100", sizeof k_amp / sizeof *k_amp, miss);
    check(line, all && procon_amp_code(1.5f) == 100 && procon_amp_code(NAN) == 0);
    uint8_t m[4]; float none[4] = {0, 160, 0, 320}, full[4] = {1.002867f, 160, 1.002867f, 320}, low[4] = {0.5f, 160, 0, 320};
    procon_hd_motor(none, m); int ok1 = m[0] == 0x00 && m[1] == 0x01 && m[2] == 0x40 && m[3] == 0x40;
    procon_hd_motor(full, m); int ok2 = m[0] == 0x00 && m[1] == 0xc9 && m[2] == 0x40 && m[3] == 0x72;
    procon_hd_motor(low, m);
    check("motor bytes: none 00 01 40 40; both bands 1.0029 00 c9 40 72; low 0.5 at 160 Hz 00 01 40 62", ok1 && ok2 && m[0] == 0x00 && m[1] == 0x01 && m[2] == 0x40 && m[3] == 0x62);
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
