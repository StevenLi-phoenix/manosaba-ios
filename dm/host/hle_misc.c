// Miscellaneous service layer: add-on content / shop / error applet / profanity filter / server name resolution / parental controls / software keyboard.
// On the real device each goes through IPC or a system applet; the host gives results as single-player offline. Pure-computation SDK parts (hash, util, software Opus) are not taken over, the original implementation is used directly.
#include "game.h"
#include "hle.h"

// ---------- aoc add-on content / ec shop ----------
// Owned add-on content = the profile's aoc list (game.json "aoc", default none). Only for DLC that is an unlock ticket whose content already ships in
// the main RomFS: the host mounts no add-on content data
HLE_FORCE("_ZN2nn3aoc17CountAddOnContentEv", "add-on content count: the profile's aoc list", int, h_aoc_Count, void) { return dm_game.aoc_n; }
// ListAddOnContent(out, start index, max count): writes the owned indices from the start index on, returns how many
HLE_FORCE("_ZN2nn3aoc16ListAddOnContentEPiii", "add-on content list: the profile's aoc list", int, h_aoc_List, int *out, int off, int n) {
    int w = 0;
    for (int i = off; i >= 0 && i < dm_game.aoc_n && w < n; i++) out[w++] = dm_game.aoc[i];
    return w;
}
// The event object is created on demand by the nn::os side table (not signaled), the game waiting on it only times out / gets not-signaled
HLE_FORCE("_ZN2nn3aoc31GetAddOnContentListChangedEventEPNS_2os11SystemEventE", "add-on content change event: bound to a kernel event, never signalled (games poll it with TryWait and abort when it is not initialized)", void, h_aoc_GetEv, void *ev) { hle_bind_system_event(ev, 0); }
HLE_FORCE("_ZN2nn2ec24ShowShopAddOnContentListEv", "shop applet (out of scope): no-op", void, h_ec_ShowShop, void) { LOG("ec::ShowShopAddOnContentList (cut)"); }

// ---------- err error applet: printed by the host ----------
HLE_FORCE("_ZN2nn3err9ShowErrorENS_6ResultE", "error applet; the host logs module / description", void, h_err_ShowError, Result r) {
    LOG("err::ShowError Result=%#x (module %u, description %u)", r, r & 0x1ff, (r >> 9) & 0x1fff);
}

// ---------- ngc profanity filter: the word list is in a system data save (mounted via IPC); single-player has no text visible to others, always judged as no profanity ----------
HLE_FORCE("_ZN2nn3ngc15ProfanityFilterC1Ev", "profanity filter (word list in system save data); not created", void, h_ngc_Ctor, void *self) { (void)self; }
HLE_FORCE("_ZN2nn3ngc15ProfanityFilterD1Ev", "profanity filter", void, h_ngc_Dtor, void *self) { (void)self; }
HLE_FORCE("_ZN2nn3ngc15ProfanityFilter10InitializeEPvm", "profanity filter initialization; no word list", Result, h_ngc_Init, void *self, void *work, uint64_t sz) { (void)self; (void)work; (void)sz; return RES_OK; }
HLE_FORCE("_ZN2nn3ngc15ProfanityFilter17GetContentVersionEv", "word list version: 0", uint32_t, h_ngc_Ver, void *self) { (void)self; return 0; }
HLE_FORCE("_ZN2nn3ngc15ProfanityFilter19CheckProfanityWordsEPjjPPKcm", "profanity check: never matches", Result, h_ngc_Check, void *self, uint32_t *masks, uint32_t pat, const char **words, uint64_t n) {
    (void)self; (void)pat; (void)words;
    for (uint64_t i = 0; i < n; i++) masks[i] = 0;
    return RES_OK;
}

// ---------- nsd server name resolution / pctl free communication: networking, out of scope ----------
HLE_FORCE("_ZN2nn3nsd7ResolveEPNS0_4FqdnERKS1_", "server name resolution (online, out of scope): fails", Result, h_nsd_Resolve, void *out, const void *in) { (void)out; (void)in; return RES_FAIL; }
HLE_FORCE("_ZN2nn3nsd9ResolveExEPNS0_4FqdnERKS1_", "server name resolution (online, out of scope): fails", Result, h_nsd_ResolveEx, void *out, const void *in) { (void)out; (void)in; return RES_FAIL; }
// bool arguments only look at the low 8 bits; returns bool
HLE_FORCE("_ZN2nn4pctl25TryBeginFreeCommunicationEb", "parental control, free communication (online, out of scope): never allowed", unsigned, h_pctl_TryBegin, unsigned show_ui) { (void)show_ui; return 0; }
HLE_FORCE("_ZN2nn4pctl20EndFreeCommunicationEv", "parental control, free communication", void, h_pctl_End, void) {}

// swkbd software keyboard: see hle_swkbd.c
