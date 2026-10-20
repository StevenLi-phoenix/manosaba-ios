// Tears of the Kingdom's pre-rendered movies held at 30 fps (plan item 130; FPSLocker's file for the game: "must be blocked to certain framerate to not
// cause issues", and totk-mobile holds them too). The game extends the user inactivity time for every cutscene: main 0x772fd8 ends in
// nn::oe::SetUserInactivityDetectionTimeExtended(true) (tail call at 0x773034) and is called by 0x770f00 when a real-time cutscene starts (the
// prologue's, measured) and by 0x2b36100 when a pre-rendered movie starts (FPSLocker's movie flag is set there); false comes from 0x770acc (the
// cutscene ends) and from 0x2b33914, called by 0x2b36770 (the movie ends; FPSLocker clears its flag there). So the call that returns to main+0x2b36104
// holds, any other releases: real-time cutscenes keep the product's rate. Dm_ED_0007_PreRender is not held: 0x772fd8 skips the call for it.
#include <string.h>
#include "dm.h"
#include "game.h"

static int movie_fps(int on, const char *mod, uint64_t off) {
#ifdef DM_TEST_MOVIE_ANY_CUTSCENE   // negative control of t_movie_fps only: every cutscene held (the oe call alone, as the review proposed)
    (void)mod; (void)off; return on ? 30 : 0;
#endif
    return on && mod && !strcmp(mod, "main") && off == 0x2b36104 ? 30 : 0;
}
#ifndef MOVIE_FPS_TEST
int dm_game_inactivity_fps(int on, uint64_t ra) {
    module_t *m = module_of((const void *)ra);
    return movie_fps(on, m ? m->name : NULL, m ? ra - (uint64_t)m->base : 0);
}
#endif
