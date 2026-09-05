/*
 * miniaudio.c - the one translation unit that carries miniaudio's
 * implementation (ext/miniaudio, a single header).  Everything the
 * server does not use is compiled out: no decoders, no encoders, no
 * generators, and only the backends a Linux desktop has.  The backends
 * are loaded with dlopen at run time, so the package depends on no
 * audio library - a machine without one gets the null backend.
 */
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_WAV
#define MA_NO_FLAC
#define MA_NO_MP3
#define MA_ENABLE_ONLY_SPECIFIC_BACKENDS
#define MA_ENABLE_PULSEAUDIO
#define MA_ENABLE_ALSA
#define MA_ENABLE_NULL
#include "miniaudio.h"
