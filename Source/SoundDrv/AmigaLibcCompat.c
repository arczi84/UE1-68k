/* The prebuilt Amiga SDL_mixer archive expects a plain C `errno` object.
 * libnix exposes errno through __errno instead, so provide the compatibility
 * symbol used internally by SDL_mixer.  Game code still uses libnix errno. */
int SDL_mixer_errno_compat asm("_errno") = 0;
