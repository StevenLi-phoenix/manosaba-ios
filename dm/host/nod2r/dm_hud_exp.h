// Stand-in for games other than D2R (make GAME=<other>): the D2R experience bar never runs (dm_is_d2r() is 0)
#pragma once
#include <stdint.h>
static const uint32_t k_exp[100] = { 0 };
