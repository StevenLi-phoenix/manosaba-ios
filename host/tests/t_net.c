// Offline socket errno (hle_net.c): a failed socket call, a failed name lookup and an unsupported address family report nn::socket's own values
// (the SDK's nn::socket::Errno: ENetDown 100, EAfNoSupport 97), through both the C++ and the C API. The FreeBSD numbers used before (50 = the
// SDK's ENoCsi, 47) fail it (-DDM_TEST_NET_BSD_ERRNO).
#include "../hle_net.c"
#include <stdio.h>
module_t g_mod[MAX_MOD]; int g_nmod;
static int checks, fails;
#define CHECK(c, s) do { checks++; int ok = !!(c); fails += !ok; printf("%-70s %s\n", s, ok ? "✓" : "✗"); } while (0)
int main(void) {
    CHECK(h_sk_Socket(2, 1, 0) == -1 && h_sk_GetLastError() == 100, "Socket fails offline: ENetDown 100");
    h_nsk_SetErrno(0);
    CHECK(h_nsk_Connect(3, NULL, 0) == -1 && h_nsk_GetErrno() == 100, "nnsocketConnect fails offline: ENetDown 100 (C API)");
    h_nsk_SetErrno(0);
    CHECK(h_sk_GetHostEnt("example.com", NULL, 0) == NULL && h_sk_GetLastError() == 100, "name resolution offline: ENetDown 100");
    unsigned char a[4];
    CHECK(h_sk_Pton(10, "::1", a) == -1 && h_sk_GetLastError() == 97, "InetPton of family 10 (IPv6): EAfNoSupport 97");
    CHECK(h_sk_Pton(2, "10.1.2.3", a) == 1 && a[0] == 10 && a[3] == 3, "InetPton of an IPv4 address still converts");
    printf("t_net: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
