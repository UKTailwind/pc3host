/*
 * sndhw.c - the hardware half of the sound, for a PC.
 *
 * sound_priv.h names what the kernel's sound core asks of its output
 * stage; sound_hw.c answers for the PC3 with PIO, DMA and an interrupt,
 * and this file answers for the server with miniaudio.  The core's
 * contract is "fill this block of 64 stereo frames", and a sound card's
 * pull callback is exactly that shape - so the callback asks the core
 * for blocks, at whichever rate the core has set (22050 for the BBC
 * synth, 44100 for the MMBasic synth, a file's own rate for PCM), and
 * resamples them to the one rate the device was opened at.  The device
 * is never reopened: a rate change is a resampler ratio.
 *
 * The lock.  On the board the fill runs in the DMA interrupt and the
 * core's lock is di(); here the fill runs on miniaudio's thread and the
 * lock is a recursive mutex, held by the callback around the fill and
 * by the server around every call into the core (dispatch.c), so the
 * two never touch the synth's state at once.  Recursive because the
 * core takes it again inside sound_cmd and sound_quiet.
 *
 * Owners.  The core knows a stream's owner as a 16-bit pid, which is
 * what a Fuzix pid is; a Linux pid is not.  So the server hands each
 * connection a 16-bit token (pc3d.c) and the core owns and reaps by
 * token; SNDIOC_PCMOWNER translates back to the real pid, which is what
 * PLAY STOP signals.
 *
 * --audio auto opens the default device; --audio null opens miniaudio's
 * null backend, which consumes frames in real time and plays nothing -
 * the gates run that way.  A machine with no audio device at all gets
 * the null backend and a note, so the ring still drains and a player
 * still finishes.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "sound.h"
#include "sound_priv.h"
#include "pc3d.h"
#include "miniaudio.h"

#define OUT_RATE   44100
#define STAGE_CAP  4096			/* frames of source-rate audio in hand */

static pthread_mutex_t lock;
static ma_context ctx;
static ma_device dev;
static ma_resampler rs;
static int have_ctx, have_dev, have_rs;
static uint32_t src_rate = SND_RATE;	/* the rate the core's blocks are at */
static uint32_t want_rate = SND_RATE;	/* what the core last asked for */
static int16_t stage[STAGE_CAP * 2];	/* blocks not yet consumed, compacted */
static unsigned stage_i, stage_n;
static const char *backend = "none";
static unsigned long blocks;

/* --- the hooks -------------------------------------------------------------------- */

snd_lock_t snd_hw_lock(void)
{
	pthread_mutex_lock(&lock);
	return 0;
}

void snd_hw_unlock(snd_lock_t l)
{
	(void)l;
	pthread_mutex_unlock(&lock);
}

/* The core calls this from process context - under the dispatch lock
 * here - and the callback applies it at its next block, under the same
 * lock, dropping whatever was staged at the old rate as the board's
 * clock divider change would. */
void snd_hw_rate(uint32_t rate)
{
	want_rate = rate;
}

int snd_hw_copyin(void *dst, const void *src, uint32_t n)
{
	memcpy(dst, src, n);		/* the bytes arrived in the request */
	return 0;
}

int snd_hw_pid_alive(uint16_t tok)
{
	return pc3d_tok_alive(tok);
}

/* --- the callback ----------------------------------------------------------------- */

static void refill(void)
{
	if (stage_i && stage_n)
		memmove(stage, stage + stage_i * 2, (size_t)stage_n * 4);
	stage_i = 0;
	sound_fill_block(stage + stage_n * 2);
	stage_n += SND_NBUF;
	blocks++;
}

static void apply_rate(void)
{
	if (want_rate == src_rate)
		return;
	src_rate = want_rate;
	stage_i = stage_n = 0;
	if (src_rate != OUT_RATE && have_rs)
		ma_resampler_set_rate(&rs, src_rate, OUT_RATE);
}

static void data_cb(ma_device *d, void *out, const void *in, ma_uint32 nframes)
{
	int16_t *o = out;
	ma_uint32 done = 0;

	(void)d;
	(void)in;
	pthread_mutex_lock(&lock);
	apply_rate();
	while (done < nframes) {
		if (src_rate == OUT_RATE || !have_rs) {
			unsigned n;

			if (stage_n == 0)
				refill();
			n = nframes - done;
			if (n > stage_n)
				n = stage_n;
			memcpy(o + done * 2, stage + stage_i * 2, (size_t)n * 4);
			stage_i += n;
			stage_n -= n;
			done += n;
		} else {
			ma_uint64 need = 0, fin, fout;

			ma_resampler_get_required_input_frame_count(&rs, nframes - done, &need);
			while (stage_n < need && stage_n + SND_NBUF <= STAGE_CAP)
				refill();
			fin = stage_n;
			fout = nframes - done;
			if (ma_resampler_process_pcm_frames(&rs, stage + stage_i * 2, &fin,
							    o + done * 2, &fout) != MA_SUCCESS)
				break;
			stage_i += (unsigned)fin;
			stage_n -= (unsigned)fin;
			done += (ma_uint32)fout;
			if (fin == 0 && fout == 0)
				break;
		}
	}
	if (done < nframes)
		memset(o + done * 2, 0, (size_t)(nframes - done) * 4);
	pthread_mutex_unlock(&lock);
}

/* --- the server's side ------------------------------------------------------------ */

void sndhw_lock(void)
{
	pthread_mutex_lock(&lock);
}

void sndhw_unlock(void)
{
	pthread_mutex_unlock(&lock);
}

/* Has the stream drained to the mark, or gone?  The kernel's tick asks
 * the same question for a sleeping player. */
int sndhw_pcm_ready(uint16_t tok, uint32_t mark)
{
	uint32_t q;
	int r;

	pthread_mutex_lock(&lock);
	r = sound_pcm_queued(tok, &q) < 0 || q <= mark;
	pthread_mutex_unlock(&lock);
	return r;
}

/* A connection ended: what pagemap_free does for a dying process. */
void sndhw_client_gone(uint16_t tok)
{
	pthread_mutex_lock(&lock);
	sound_pcm_close(tok);
	sound_mm_owner_gone(tok);
	pthread_mutex_unlock(&lock);
}

const char *sndhw_backend(void)
{
	return backend;
}

unsigned long sndhw_blocks(void)
{
	return blocks;
}

static int open_device(const ma_backend *which, int n)
{
	ma_device_config cfg;

	if (ma_context_init(which, (ma_uint32)n, NULL, &ctx) != MA_SUCCESS)
		return -1;
	have_ctx = 1;
	cfg = ma_device_config_init(ma_device_type_playback);
	cfg.playback.format = ma_format_s16;
	cfg.playback.channels = 2;
	cfg.sampleRate = OUT_RATE;
	cfg.dataCallback = data_cb;
	/* 10 ms periods: the board's block is 1.45 ms at 44100 and a
	 * PLAY SOUND change is heard within it; this is as close as a PC
	 * device sensibly gets, and WSLg adds its own hundred on top. */
	cfg.periodSizeInMilliseconds = 10;
	if (ma_device_init(&ctx, &cfg, &dev) != MA_SUCCESS) {
		ma_context_uninit(&ctx);
		have_ctx = 0;
		return -1;
	}
	have_dev = 1;
	if (ma_device_start(&dev) != MA_SUCCESS) {
		ma_device_uninit(&dev);
		ma_context_uninit(&ctx);
		have_dev = have_ctx = 0;
		return -1;
	}
	backend = ma_get_backend_name(ctx.backend);
	return 0;
}

int sndhw_init(const char *mode)
{
	pthread_mutexattr_t a;
	ma_resampler_config rc;
	static const ma_backend nul[] = { ma_backend_null };

	pthread_mutexattr_init(&a);
	pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&lock, &a);

	rc = ma_resampler_config_init(ma_format_s16, 2, src_rate, OUT_RATE,
				      ma_resample_algorithm_linear);
	if (ma_resampler_init(&rc, NULL, &rs) == MA_SUCCESS)
		have_rs = 1;

	if (mode && !strcmp(mode, "null"))
		return open_device(nul, 1);
	if (open_device(NULL, 0) == 0)
		return 0;
	fprintf(stderr, "pc3d: no audio device; sound plays into the void\n");
	return open_device(nul, 1);
}

void sndhw_close(void)
{
	if (have_dev)
		ma_device_uninit(&dev);
	if (have_ctx)
		ma_context_uninit(&ctx);
	if (have_rs)
		ma_resampler_uninit(&rs, NULL);
	have_dev = have_ctx = have_rs = 0;
}
