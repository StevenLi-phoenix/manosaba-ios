// A Switch Pro Controller's own reports (plan item 67b; host/front/procon_mac.m reads and writes them over IOHID on the Mac). Pure functions, tested by
// host/tests/t_procon.c.
//   IMU: the standard input report 0x30 (49 bytes and more) carries three samples at 13, 25 and 37, each acceleration then gyro as three int16;
//   ±8 G = 4096 LSB per G, ±2000 degrees a second = 16.384 LSB per degree a second. Raw axes x forward (towards the shoulder buttons), y left, z up,
//   the acceleration the reaction to gravity (+1 G up at rest); nn::hid takes acceleration (y, -x, -z), along gravity (flat: (0, 0, -1)), and gyro
//   (-y, x, z). atone/nxnative f6dba3d / 185cfda measured this on a Pro Controller (flat, on its shoulder edge, on its right grip) and checked it
//   with Breath of the Wild's bow aiming (MIT; written anew). The three samples are averaged.
//   HD rumble: output report 0x10 = {0x10, packet counter 0-15, left motor 4 bytes, right motor 4 bytes}; a motor's bytes are its high band's
//   frequency and amplitude, then its low band's (dekuNukem's Nintendo_Switch_Reverse_Engineering rumble_data_table: frequency code
//   round(log2(f / 10) * 32), the high band's 4 * (code - 0x60) over 81.75-1252.57 Hz, the low band's code - 0x40 over 40.875-626.29 Hz;
//   the amplitude code from its table: 0.007843 is 1, then 2^(1/4) apart from 0.011823 (2) to 0.112491 (15), round(log2(a * 17) * 16) to
//   0.23, round(log2(a * 8.7) * 32) above, at most 100 (1.0029; from 101 on the table warns it is not safe for the actuators); the high band's
//   byte is 2 * code, the low band's code's low bit in bit 7 and 0x40 + code / 2 in the next byte). No vibration: {0x00, 0x01, 0x40, 0x40}.
#pragma once
#include <math.h>
#include <stdint.h>

static inline int procon_imu(const uint8_t *rep, long len, float acc[3], float gyro_rps[3]) {   // 1 for a report 0x30 with its IMU
    if (!rep || len < 49 || rep[0] != 0x30) return 0;
    double ra[3] = {0}, rg[3] = {0};
    for (int f = 0; f < 3; f++) {
        const uint8_t *v = rep + 13 + 12 * f;
        for (int i = 0; i < 3; i++) { ra[i] += (int16_t)(v[2 * i] | v[2 * i + 1] << 8) / 3.0; rg[i] += (int16_t)(v[6 + 2 * i] | v[7 + 2 * i] << 8) / 3.0; }
    }
#ifndef DM_TEST_PROCON_RAW_AXES   // negative control of t_procon only: the raw axes as they come
    acc[0] = (float)(ra[1] / 4096); acc[1] = (float)(-ra[0] / 4096); acc[2] = (float)(-ra[2] / 4096);
    gyro_rps[0] = (float)(-rg[1] / 16.384 / 360); gyro_rps[1] = (float)(rg[0] / 16.384 / 360); gyro_rps[2] = (float)(rg[2] / 16.384 / 360);
#else
    for (int i = 0; i < 3; i++) { acc[i] = (float)(ra[i] / 4096); gyro_rps[i] = (float)(rg[i] / 16.384 / 360); }
#endif
    return 1;
}
static inline int procon_amp_code(float a) {
    if (!(a > 0)) return 0;
    int c;
    if (a < 0.1150f) c = (int)lrintf(4 * log2f(a / 0.011823f) + 2);   // (0.112491 is 15, 0.117471 is 16: the boundary between them)
    else if (a < 0.2274f) c = (int)lrintf(log2f(a * 17) * 16);
    else c = (int)lrintf(log2f(a * 8.7f) * 32);
    return c < 1 ? 1 : c > 100 ? 100 : c;
}
static inline void procon_hd_motor(const float v[4], uint8_t o[4]) {   // v = {amplitude low, frequency low, amplitude high, frequency high}
    int la = procon_amp_code(v[0]), ha = procon_amp_code(v[2]);
    if (!la && !ha) { o[0] = 0x00; o[1] = 0x01; o[2] = 0x40; o[3] = 0x40; return; }
    float fl = v[1] > 0 ? v[1] : 160, fh = v[3] > 0 ? v[3] : 320;
    fl = fl < 40.875f ? 40.875f : fl > 626.29f ? 626.29f : fl; fh = fh < 81.75f ? 81.75f : fh > 1252.57f ? 1252.57f : fh;
    int lf = (int)lrintf(log2f(fl / 10) * 32) - 0x40, hf = ((int)lrintf(log2f(fh / 10) * 32) - 0x60) * 4;
    lf = lf < 1 ? 1 : lf > 0x7f ? 0x7f : lf; hf = hf < 4 ? 4 : hf > 0x1fc ? 0x1fc : hf;
    o[0] = (uint8_t)(hf & 0xff); o[1] = (uint8_t)((ha << 1) | ((hf >> 8) & 1)); o[2] = (uint8_t)(lf | (la & 1) << 7); o[3] = (uint8_t)(0x40 + (la >> 1));
}
