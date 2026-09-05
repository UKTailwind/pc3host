/*
 * sound_seam.c - the kernel's sound core, compiled without the kernel.
 *
 * sound.c is built with -DPC3_HOST against the five hooks below, which
 * record rather than do, and driven the way misc.c drives it: the
 * ownership rules, the PCM ring, mono expansion, underrun counting, the
 * BBC square wave, and the MMBasic synth - whose 5 seconds of 440 Hz
 * sine are written to mm.wav for sound_seam.sh to compare against
 * sndharness, playsnd's own renderer, sample for sample.  If the two
 * ever differ the kernel synth has drifted from the daemon it replaced.
 *
 *   sound_seam <work dir>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "sound.h"
#include "sound_priv.h"

/* --- the hooks: a machine that remembers ---------------------------------- */

static uint32_t hw_rate = SND_RATE;
static unsigned char alive[65536];

snd_lock_t snd_hw_lock(void) { return 0; }
void snd_hw_unlock(snd_lock_t l) { (void)l; }
void snd_hw_rate(uint32_t rate) { hw_rate = rate; }
int snd_hw_copyin(void *dst, const void *src, uint32_t n)
{
	memcpy(dst, src, n);
	return 0;
}
int snd_hw_pid_alive(uint16_t pid) { return alive[pid]; }

/* --- the checks ------------------------------------------------------------- */

static int fails;

static void check(const char *what, int ok)
{
	printf("%s  %s\n", ok ? "pass" : "FAIL", what);
	if (!ok)
		fails++;
}

static int16_t blk[SND_NBUF * 2];

/* mmb_playctl.h's numbers, as pico_ioctl.h pins them */
#define OP_SOUND 1
#define OP_TONE 2
#define OP_VOLUME 4
#define SND_SINE 1

static void wav_header(FILE *f, unsigned long nframes, unsigned long rate)
{
	unsigned long datalen = nframes * 4, riff = 36 + datalen, br = rate * 4;
	unsigned char h[44];

	memcpy(h, "RIFF", 4);
	h[4] = riff; h[5] = riff >> 8; h[6] = riff >> 16; h[7] = riff >> 24;
	memcpy(h + 8, "WAVEfmt ", 8);
	h[16] = 16; h[17] = h[18] = h[19] = 0;
	h[20] = 1; h[21] = 0;
	h[22] = 2; h[23] = 0;
	h[24] = rate & 255; h[25] = (rate >> 8) & 255; h[26] = h[27] = 0;
	h[28] = br; h[29] = br >> 8; h[30] = br >> 16; h[31] = br >> 24;
	h[32] = 4; h[33] = 0;
	h[34] = 16; h[35] = 0;
	memcpy(h + 36, "data", 4);
	h[40] = datalen; h[41] = datalen >> 8;
	h[42] = datalen >> 16; h[43] = datalen >> 24;
	fwrite(h, 1, 44, f);
}

/* The MMBasic synth: voice 1, sine, 440 Hz, both sides, volume 25 at
 * PLAY VOLUME 70 - what sndharness renders - for 5 seconds. */
static void mm_synth_wav(const char *dir)
{
	char path[4096];
	FILE *f;
	unsigned long total = 44100UL * 5, done = 0;

	alive[1] = 1;
	check("MMCMD VOLUME accepted", sound_mm_cmd(OP_VOLUME, 0, 0, 70, 70, 0, 1) == 0);
	check("MMCMD SOUND accepted", sound_mm_cmd(OP_SOUND, 1, 3, SND_SINE, 440000, 25, 1) == 0);
	check("synth runs the output at 44100", hw_rate == MMS_RATE);
	check("the synth owns the output", sound_pcm_owner() == 1);

	snprintf(path, sizeof path, "%s/mm.wav", dir);
	f = fopen(path, "wb");
	if (!f) {
		perror(path);
		fails++;
		return;
	}
	wav_header(f, total, MMS_RATE);
	while (done < total) {
		unsigned long n = total - done;

		sound_fill_block(blk);
		if (n > SND_NBUF)
			n = SND_NBUF;
		fwrite(blk, 4, n, f);
		done += n;
	}
	fclose(f);
	sound_mm_stop();
	check("MMSTOP hands the output back at 22050", hw_rate == SND_RATE && sound_pcm_owner() == 0);
}

static void pcm_rules(void)
{
	int16_t in[SND_NBUF * 2];
	uint32_t space, queued, under;
	int i, same;

	alive[2] = alive[3] = 1;
	for (i = 0; i < SND_NBUF * 2; i++)
		in[i] = (int16_t)(i * 37 - 2000);

	check("PCMOPEN by 2", sound_pcm_open(44100, 2, 2) == 0);
	check("the stream sets the rate", hw_rate == 44100);
	check("PCMOPEN by 3 refused while 2 holds it", sound_pcm_open(44100, 2, 3) == -2);
	check("PCMWRITE by 3 refused", sound_pcm_write((uint8_t *)in, 16, 3) == -1);
	sound_pcm_close(3);
	check("PCMCLOSE by 3 ignored", sound_pcm_owner() == 2);
	check("PCMWRITE by 2 takes a whole block",
	      sound_pcm_write((uint8_t *)in, sizeof in, 2) == (int)sizeof in);
	sound_pcm_stat(&space, &queued, &under);
	check("STAT counts it queued", queued == sizeof in && under == 0);
	sound_fill_block(blk);
	same = memcmp(blk, in, sizeof in) == 0;
	check("the block comes out as it went in", same);
	sound_pcm_stat(&space, &queued, &under);
	check("STAT sees it drained, no underrun", queued == 0 && under == 0);
	sound_fill_block(blk);
	sound_pcm_stat(&space, &queued, &under);
	check("an empty ring after the start is one underrun", under == 1);
	check("a partial frame is never accepted",
	      sound_pcm_write((uint8_t *)in, 3, 2) == 0);
	alive[2] = 0;
	check("a dead owner's stream is reaped", sound_pcm_owner() == 0 && hw_rate == SND_RATE);

	/* mono: the driver duplicates, the player pays nothing */
	check("PCMOPEN mono at 22050", sound_pcm_open(22050, 1, 3) == 0 && hw_rate == 22050);
	check("PCMWRITE of 64 mono samples", sound_pcm_write((uint8_t *)in, SND_NBUF * 2, 3) == SND_NBUF * 2);
	sound_fill_block(blk);
	same = 1;
	for (i = 0; i < SND_NBUF; i++)
		if (blk[i * 2] != in[i] || blk[i * 2 + 1] != in[i])
			same = 0;
	check("mono lands in both channels", same);
	sound_pcm_close(3);
	check("PCMCLOSE by the owner releases", sound_pcm_owner() == 0);

	/* an empty ring before the first sample is not an underrun */
	check("PCMOPEN again", sound_pcm_open(44100, 2, 3) == 0);
	sound_fill_block(blk);
	sound_fill_block(blk);
	sound_pcm_stat(&space, &queued, &under);
	check("silence before the stream begins is not counted", under == 0);
	check("the space is the ring", space == 256u * 1024u);

	/* the synth and a player exclude each other, as MMBasic's do */
	alive[4] = 1;
	check("MMCMD refused while a player holds the output",
	      sound_mm_cmd(OP_SOUND, 1, 3, SND_SINE, 440000, 25, 4) == -2);
	sound_pcm_close(3);
	check("MMCMD accepted once it is free",
	      sound_mm_cmd(OP_SOUND, 1, 3, SND_SINE, 440000, 25, 4) == 0 && hw_rate == MMS_RATE);
	check("PCMOPEN refused while the synth holds it", sound_pcm_open(44100, 2, 3) == -2);
	sound_mm_owner_gone(4);
	check("a dying program's synth claim is released",
	      sound_pcm_owner() == 0 && hw_rate == SND_RATE);
}

/* The BBC synth: SOUND 1,-15,89,20 is A4 for a second.  Count the rising
 * zero crossings over the first half second: 440 Hz gives 220. */
static void bbc_square(void)
{
	int i, n, crossings = 0, nonzero = 0;
	int16_t prev = 0;

	check("SOUND 1,-15,89,20 queued", sound_cmd(1, -15, 89, 20) == 0);
	for (n = 0; n < (SND_RATE / 2) / SND_NBUF; n++) {
		sound_fill_block(blk);
		for (i = 0; i < SND_NBUF; i++) {
			int16_t v = blk[i * 2];
			if (v)
				nonzero = 1;
			if (prev <= 0 && v > 0)
				crossings++;
			prev = v;
		}
	}
	printf("      %d rising crossings in half a second (220 expected)\n", crossings);
	check("the square sounds", nonzero);
	check("pitch 89 is 440 Hz", crossings >= 214 && crossings <= 226);

	/* the queue: eight notes, then EAGAIN */
	sound_quiet();
	check("QUIET empties the queue", sound_qfree(1) == 8);
	for (i = 0; i < 8; i++)
		sound_cmd(1, -15, 89 + i, 5);
	/* the first note starts playing at once, so one slot is back */
	check("nine notes fill the queue", sound_cmd(1, -15, 100, 5) == 0 && sound_cmd(1, -15, 101, 5) == -1);
	sound_quiet();
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: sound_seam <work dir>\n");
		return 2;
	}
	pcm_rules();
	bbc_square();
	mm_synth_wav(argv[1]);
	printf("sound_seam: %s\n", fails ? "FAILED" : "all passed");
	return fails ? 1 : 0;
}
