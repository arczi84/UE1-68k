#ifdef PLATFORM_AMIGA
/*
 * The available 68k OpenAL archive was built against a libc exposing errno
 * as a global variable, while this executable uses libnix.  OpenAL's WAV
 * loader is the only archive member referencing that legacy symbol.
 */
int errno;
#endif
