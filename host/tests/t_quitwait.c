// Quit waits for the game (hle_lifecycle.c quit_wait): nothing pending returns at once; inside the exit-request section (hle_oe.c, entered twice,
// left twice from another thread 200 ms later) it waits until the game leaves it; a section never left is waited for until the limit and reported;
// a pending save and the section both held are waited for until the later ends. The old wait, the save alone (-DDM_TEST_QUIT_NO_SECTION), fails 3.
#include "../hle_lifecycle.c"
#include <pthread.h>
#include <stdio.h>
module_t g_mod[MAX_MOD]; int g_nmod;
uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC); }
void hlecount_flush(void) {}
void dm_audio_suspend(int on) { (void)on; }
void dm_gfx_suspend(int on) { (void)on; }
static _Atomic int g_save, g_sec;
int dm_fs_save_pending(void) { return atomic_load(&g_save); }
int dm_oe_exit_section(void) { return atomic_load(&g_sec) > 0; }
static int checks, fails;
#define CHECK(c, s) do { checks++; int ok = !!(c); fails += !ok; printf("%-74s %s\n", s, ok ? "✓" : "✗"); } while (0)
static void *leave_later(void *p) { usleep(200000); atomic_fetch_sub(&g_sec, 1); usleep(20000); atomic_fetch_sub(&g_sec, 1); (void)p; return NULL; }
static void *commit_later(void *p) { usleep(100000); atomic_store(&g_save, 0); (void)p; return NULL; }
int main(void) {
    int save = 9, sec = 9; double w = quit_wait(15, &save, &sec);
    CHECK(w < 0.05 && !save && !sec, "nothing pending: returns at once");
    atomic_store(&g_sec, 2); pthread_t t; pthread_create(&t, NULL, leave_later, NULL);
    w = quit_wait(15, &save, &sec); pthread_join(t, NULL);
    CHECK(w >= 0.2 && w < 1.0, "inside the exit-request section (twice): waits until the game leaves it");
    CHECK(!sec && !save, "... and then nothing holds");
    atomic_store(&g_sec, 1); w = quit_wait(0.3, &save, &sec);
    CHECK(w >= 0.29 && w < 0.6 && sec == 1, "a section never left: the limit, reported as held");
    atomic_store(&g_sec, 2); atomic_store(&g_save, 1); pthread_t a, b;
    pthread_create(&a, NULL, commit_later, NULL); pthread_create(&b, NULL, leave_later, NULL);
    w = quit_wait(15, &save, &sec); pthread_join(a, NULL); pthread_join(b, NULL);
    CHECK(w >= 0.2 && !save && !sec, "a pending save and the section: waits for the later (the section)");
    printf("t_quitwait: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
