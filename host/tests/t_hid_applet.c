// The controller support applet and the preselected user (plan item 64k). ShowControllerSupport with a result: one player and the Npad the
// operation mode connects (No.1, 0, docked; handheld, 0x20, in handheld mode), at +0 and +4 of ControllerSupportResultInfo (SDK reference), the
// padding left alone; the variant without a result succeeds. OpenPreselectedUser (the Result variant) opens the same user as TryOpenPreselectedUser.
// Nothing written, as the stub (-DDM_TEST_CTLSUPPORT_SILENT): 2 fail.
#include "../hle/hle_hid.c"
#include "../hle/hle_account.c"
module_t g_mod[MAX_MOD]; int g_nmod;
void hle_bind_system_event(void *g, int autoclear) { (void)g; (void)autoclear; }
void hle_signal_system_event(void *g) { (void)g; }
uint64_t now_ns(void) { return 1000000000ull; }
uint64_t guest_ns(void) { return 1000000000ull; }
uint64_t dm_nvn_frame_count(void) { return 0; }
static int opmode = 1;
int dm_opmode(void) { return opmode; }
int touch_fill(void *p, uint64_t n) { (void)p; (void)n; return 0; }
void dm_touch_set(const dm_touch *p, int n) { (void)p; (void)n; }
static int n, fail;
static void check(const char *what, int ok) { n++; fail += !ok; printf("%-100s %s\n", what, ok ? "✓" : "✗"); }
int main(void) {
    uint8_t info[8], arg[64] = {0}; uint32_t id;
    memset(info, 0xa5, sizeof info); opmode = 1; Result r = h_hid_CtlSupport(info, arg); memcpy(&id, info + 4, 4);
    check("docked: success, one player, Npad No.1 (0), the padding left alone", !r && info[0] == 1 && id == 0 && info[1] == 0xa5 && info[3] == 0xa5);
    memset(info, 0xa5, sizeof info); opmode = 0; h_hid_CtlSupport(info, arg); memcpy(&id, info + 4, 4);
    check("handheld: one player, the handheld Npad (0x20)", info[0] == 1 && id == 0x20);
    check("the variant without a result: success", !h_hid_CtlSupport0(arg));
    UserHandle a, b; Uid ua, ub; memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    int ok = h_acc_TryOpenPre(&a) && !h_acc_OpenPre(&b) && !h_acc_GetUserId(&ua, &a) && !h_acc_GetUserId(&ub, &b);
    check("OpenPreselectedUser: success, the same user as TryOpenPreselectedUser", ok && !memcmp(&ua, &ub, sizeof ua));
    printf("== %d checks, %d failed ==\n", n, fail);
    return fail != 0;
}
