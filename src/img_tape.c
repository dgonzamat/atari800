/*
 * img_tape.c - support for CAS and raw tape images
 *
 * Copyright (C) 2001 Piotr Fusik
 * Copyright (C) 2001-2011 Atari800 development team (see DOC/CREDITS)
 *
 * This file is part of the Atari800 emulator project which emulates
 * the Atari 400, 800, 800XL, 130XE, and 5200 8-bit computers.
 *
 * Atari800 is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Atari800 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Atari800; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "atari.h"
#include "cassette.h"
#include "img_tape.h"
#include "memory.h"
#include "sio.h"
#include "util.h"

enum { MAX_BLOCKS = 2048 };

/* Standard record length, needed by ReadRecord() when reading raw files */
enum { DEFAULT_BUFFER_SIZE = 132 };

/* Baudrate for all written blocks and for reading from raw files. */
enum { DEFAULT_BAUDRATE = 600 };

struct IMG_TAPE_t {
	FILE *file; /* Stream for reading/writing of the tape image */
	int isCAS; /* Indicates if the file is in CAS format, or a raw binary file */
	UBYTE *buffer; /* Holds bytes of the last read or currently written data block */
	size_t buffer_size; /* Size of the space allocated for BUFFER */
	ULONG savetime; /* Time elapsed since last byte writing, in CPU ticks */
	ULONG save_gap; /* Length of the IRG before the currently written block */
	int next_blockbyte; /* Index of the byte in this block that will be read next (counted from 0) */
	unsigned int current_block; /* Number of the currently-read/written block (counted from 0) */
	int block_is_fsk; /* FALSE - current chunk's type  is "data", otherwise "fsk " */
	int block_is_wav; /* TRUE if current chunk's type is "wavp" (see WAV_OpenAsCAS()) - mutually exclusive with block_is_fsk */
	int block_length; /* Length of the block currently held in BUFFER */
	int num_blocks; /* Number of data blocks in the whole file */
	ULONG block_offsets[MAX_BLOCKS]; /* File offsets for each data block*/
	int block_baudrates[MAX_BLOCKS]; /* Baudrates for each data block in the file */
	char description[CASSETTE_DESCRIPTION_MAX]; /* Tape description, only for CAS files */
	int was_writing; /* Indicated if the last operation on the file was writing */
};

typedef struct {
	char identifier[4];
	UBYTE length_lo;
	UBYTE length_hi;
	UBYTE aux_lo;
	UBYTE aux_hi;
} CAS_Header;
/*
Just for remembering - CAS format in short:
It consists of chunks. Each chunk has a header, possibly followed by data.
If a header is unknown or unexpected it may be skipped by the length of the
header (8 bytes), and additionally the length given in the length-field(s).
There are (until now) 3 types of chunks:
-CAS file marker, has to be at begin of file - identifier "FUJI", length is
number of characters of an (optional) ascii-name (without trailing 0), aux
is always 0.
-baud rate selector - identifier "baud", length always 0, aux is rate in baud
(usually 600; one byte is 8 bits + startbit + stopbit, makes 60 bytes per
second).
-data record - identifier "data", length is length of the data block (usually
$84 as used by the OS), aux is length of mark tone (including leader and gaps)
just before the record data in milliseconds.
-raw signal stream - identifier "fsk ", length and aux the same as in "data"
chunk. Each 2 bytes in this chunk are a 16-bit number that represents length of
a MARK or SPACE signal in 1/10s of milliseconds. (So, the chunk contains
length/2 words.) The chunk starts with the SPACE signal (first 2 bytes), then
the MARK signal (next 2 bytes) and alternates between SPACE and MARK till the
end of the chunk.
*/

int IMG_TAPE_FileSupported(UBYTE const start_bytes[4])
{
	/* Note: doesn't detect raw binary files. */
	return (start_bytes[0] == 'F' && start_bytes[1] == 'U'
	     && start_bytes[2] == 'J' && start_bytes[3] == 'I')
	    || (start_bytes[0] == 'R' && start_bytes[1] == 'I'
	     && start_bytes[2] == 'F' && start_bytes[3] == 'F');
}

/* --- WAV (raw audio) tape support --------------------------------------
   Some cassette turbo loaders (e.g. the Chilean "TurboSoft"/STAC scheme)
   use signal timing that plain CAS "fsk " chunks can't describe (custom
   pulse-width protocols, not simple two-tone FSK). Real WAV captures of
   such tapes don't have this problem: they hold the actual analog
   waveform. To play them back we run the waveform through a
   Schmitt-trigger comparator (the same technique real cassette players'
   data separators - and Altirra's "load as audio" feature - use) and
   re-express the result as a sequence of exact pulse widths, letting the
   game's own loader routine decode the timing exactly as it would from
   real tape hardware - we don't need to understand its protocol at all.

   Note this is *not* stored as a CAS "fsk " chunk: "fsk " pulse widths are
   16-bit values in 1/10 ms (100us) units, which is far too coarse for an
   audio carrier - a ~5kHz FSK tone has a half-cycle around 100us itself,
   so encoding it that way would quantize away the very distinction
   between its MARK and SPACE tones. Instead we synthesize an in-memory
   CAS-like stream using a private "wavp" chunk type that stores pulse
   widths as exact, unquantized CPU-tick counts (32-bit), and give it its
   own (tiny) playback branch in IMG_TAPE_Read()/IMG_TAPE_SerinStatus(),
   alongside the existing "data"/"fsk " ones. */

/* Hysteresis band, as a percentage of the peak sample amplitude in the
   file. Wide enough to reject tape hiss, narrow enough to still catch a
   soft recording. */
enum { WAV_HYSTERESIS_PERCENT = 25 };

/* Max number of pulse-width bytes that fit in a single "wavp" chunk - the
   CAS chunk length field is a 16-bit byte count. Pulses are 4 bytes each. */
enum { WAV_MAX_CHUNK_BYTES = 65532 };

typedef struct {
	FILE *out;                    /* Where finished chunks are flushed to */
	UBYTE buffer[WAV_MAX_CHUNK_BYTES];
	int buffer_used;
} WAV_ChunkWriter;

static void WAV_ChunkWriterInit(WAV_ChunkWriter *w, FILE *out)
{
	w->out = out;
	w->buffer_used = 0;
}

/* Flush whatever pulses are queued as one "wavp" chunk. Safe to call with
   an empty buffer (writes nothing). Returns FALSE on write error. */
static int WAV_ChunkWriterFlush(WAV_ChunkWriter *w)
{
	CAS_Header header;
	if (w->buffer_used == 0)
		return TRUE;
	memcpy(header.identifier, "wavp", 4);
	header.length_lo = w->buffer_used & 0xFF;
	header.length_hi = (w->buffer_used >> 8) & 0xFF;
	header.aux_lo = 0;
	header.aux_hi = 0;
	if (fwrite(&header, 1, 8, w->out) != 8
	    || fwrite(w->buffer, 1, w->buffer_used, w->out) != (size_t)w->buffer_used)
		return FALSE;
	w->buffer_used = 0;
	return TRUE;
}

/* Queue one pulse-width value, in exact CPU ticks. Transparently flushes a
   full chunk to disk. Returns FALSE on write error. */
static int WAV_ChunkWriterAdd(WAV_ChunkWriter *w, ULONG duration_ticks)
{
	if (w->buffer_used + 4 > WAV_MAX_CHUNK_BYTES) {
		if (!WAV_ChunkWriterFlush(w))
			return FALSE;
	}
	w->buffer[w->buffer_used++] = duration_ticks & 0xFF;
	w->buffer[w->buffer_used++] = (duration_ticks >> 8) & 0xFF;
	w->buffer[w->buffer_used++] = (duration_ticks >> 16) & 0xFF;
	w->buffer[w->buffer_used++] = (duration_ticks >> 24) & 0xFF;
	return TRUE;
}

/* CPU (PAL/NTSC-ish 1.79MHz) ticks per audio sample at SAMPLE_RATE Hz. */
static double WAV_TicksPerSample(int sample_rate)
{
	return 1789790.0 / sample_rate;
}

/* Emit a physical run of NUM_SAMPLES (at SAMPLE_RATE Hz) as one or more
   pulses, splitting it only in the (practically unreachable) case of a
   single run too long to fit a 32-bit tick count. */
static int WAV_EmitRun(WAV_ChunkWriter *w, long num_samples, int sample_rate)
{
	double ticks_per_sample = WAV_TicksPerSample(sample_rate);
	/* Largest sample count whose tick duration still fits in ULONG. */
	long max_samples = (long)(4000000000.0 / ticks_per_sample);
	while (num_samples > 0) {
		long chunk_samples = num_samples > max_samples ? max_samples : num_samples;
		ULONG duration_ticks = (ULONG)(chunk_samples * ticks_per_sample + 0.5);
		if (duration_ticks < 1)
			duration_ticks = 1;
		if (!WAV_ChunkWriterAdd(w, duration_ticks))
			return FALSE;
		num_samples -= chunk_samples;
	}
	return TRUE;
}

/* Reads the WAV "fmt " and "data" sub-chunks. FILE must be positioned right
   after the 12-byte "RIFF"/size/"WAVE" header. Returns FALSE if the file
   isn't a supported (integer PCM) WAV. */
static int WAV_ParseHeader(FILE *f, int *channels, int *sample_rate,
                            int *bits_per_sample, long *data_offset, long *data_size)
{
	int have_fmt = FALSE;
	*data_offset = -1;

	for (;;) {
		char id[4];
		UBYTE size_bytes[4];
		unsigned long size;
		long chunk_start;

		if (fread(id, 1, 4, f) != 4 || fread(size_bytes, 1, 4, f) != 4)
			break;
		size = (unsigned long) size_bytes[0] | ((unsigned long) size_bytes[1] << 8)
		     | ((unsigned long) size_bytes[2] << 16) | ((unsigned long) size_bytes[3] << 24);
		chunk_start = ftell(f);

		if (memcmp(id, "fmt ", 4) == 0 && size >= 16) {
			UBYTE fmt[16];
			int audio_format;
			if (fread(fmt, 1, 16, f) != 16)
				break;
			audio_format = fmt[0] | (fmt[1] << 8);
			*channels = fmt[2] | (fmt[3] << 8);
			*sample_rate = fmt[4] | (fmt[5] << 8) | (fmt[6] << 16) | (fmt[7] << 24);
			*bits_per_sample = fmt[14] | (fmt[15] << 8);
			/* Only plain integer PCM is supported. */
			have_fmt = (audio_format == 1) && *channels > 0
			        && (*bits_per_sample == 8 || *bits_per_sample == 16
			            || *bits_per_sample == 24 || *bits_per_sample == 32);
		}
		else if (memcmp(id, "data", 4) == 0) {
			*data_offset = chunk_start;
			*data_size = (long) size;
			if (have_fmt)
				/* Both chunks found - the rest of the file doesn't matter. */
				return TRUE;
		}
		/* Chunks are padded to an even number of bytes. */
		if (fseek(f, chunk_start + size + (size & 1), SEEK_SET) != 0)
			break;
	}
	return have_fmt && *data_offset >= 0;
}

/* Reads one frame's worth of bytes and returns the downmixed, sign-extended
   sample value. */
static long WAV_DownmixFrame(const UBYTE *p, int channels, int bytes_per_sample)
{
	long sum = 0;
	int ch;
	for (ch = 0; ch < channels; ch++) {
		long v;
		const UBYTE *s = p + ch * bytes_per_sample;
		switch (bytes_per_sample) {
		case 1:
			v = (long) s[0] - 128; /* 8-bit WAV samples are unsigned */
			break;
		case 2:
			v = (SWORD) (s[0] | (s[1] << 8));
			break;
		case 3:
			v = (long) (s[0] | (s[1] << 8) | (s[2] << 16));
			if (v & 0x800000)
				v -= 0x1000000;
			break;
		default: /* 4 */
			v = (long) (s[0] | (s[1] << 8) | (s[2] << 16) | ((unsigned long) s[3] << 24));
			break;
		}
		sum += v;
	}
	return sum / channels;
}

enum { WAV_READ_FRAMES = 8192 };

/* Callback used by WAV_ScanHalfCycles(): called once per half-cycle (the
   run of samples between two consecutive amplitude-threshold crossings).
   RUN_SAMPLES is that half-cycle's true length, to be used for timing.
   SMOOTHED_SAMPLES is a 3-tap moving average of the current and previous
   two half-cycle lengths, to be used for tone classification instead -
   at only ~4-6 samples per half-cycle (a several-kHz carrier at 44.1kHz),
   a single half-cycle's length is too jittery (real tape wow-and-flutter,
   plus the coarse sample-to-carrier ratio) to reliably tell two nearby
   tones apart on its own, so classification is smoothed while timing
   isn't. Returns FALSE to abort the scan. */
typedef int (*WAV_HalfCycleFn)(void *ctx, long run_samples, long smoothed_samples);

/* Runs the amplitude Schmitt-trigger comparator over the whole PCM data
   and calls FN once per half-cycle. This is the shared core of both the
   frequency-calibration pass and the final classify-and-emit pass below -
   both need the exact same sequence of half-cycle lengths. */
static int WAV_ScanHalfCycles(FILE *f, int channels, int bytes_per_sample, int frame_size,
                               long data_offset, long num_frames, UBYTE *block,
                               int hi_thresh, int lo_thresh, WAV_HalfCycleFn fn, void *ctx)
{
	int state = 0;
	long run_samples = 0;
	long frames_left;
	long smooth_a = -1, smooth_b = -1; /* previous two half-cycle lengths, or -1 if not seen yet */

	if (fseek(f, data_offset, SEEK_SET) != 0)
		return FALSE;
	frames_left = num_frames;
	while (frames_left > 0) {
		long want = frames_left > WAV_READ_FRAMES ? WAV_READ_FRAMES : frames_left;
		long got = (long) fread(block, frame_size, want, f);
		long i;
		if (got <= 0)
			break;
		for (i = 0; i < got; i++) {
			long v = WAV_DownmixFrame(block + i * frame_size, channels, bytes_per_sample);
			int new_state = state;
			if (state == 0 && v > hi_thresh)
				new_state = 1;
			else if (state == 1 && v < lo_thresh)
				new_state = 0;
			if (new_state != state) {
				long smoothed = smooth_a < 0 ? run_samples
				              : smooth_b < 0 ? (smooth_a + run_samples) / 2
				              : (smooth_a + smooth_b + run_samples) / 3;
				if (!fn(ctx, run_samples, smoothed))
					return FALSE;
				smooth_a = smooth_b;
				smooth_b = run_samples;
				run_samples = 0;
				state = new_state;
			}
			run_samples++;
		}
		frames_left -= got;
	}
	if (run_samples > 0) {
		long smoothed = smooth_a < 0 ? run_samples
		              : smooth_b < 0 ? (smooth_a + run_samples) / 2
		              : (smooth_a + smooth_b + run_samples) / 3;
		if (!fn(ctx, run_samples, smoothed))
			return FALSE;
	}
	return TRUE;
}

/* Longest half-cycle length (in samples) tracked by the frequency
   histogram below; longer runs (silence, leader gaps) are folded into the
   last bucket - harmless, since they land far from the two tone clusters
   the histogram is trying to separate. */
enum { WAV_HIST_SIZE = 256 };

/* A real cassette player demodulates its two-tone FSK carrier in hardware
   before POKEY ever sees it: what the OS's tape-reading routine expects is
   a clean signal that only changes level at bit boundaries, not one
   transition per carrier half-cycle. WAV_ScanHalfCycles() above gives us
   the raw carrier's half-cycle lengths (in samples) - this splits them
   into the two tone-frequency clusters via Otsu's method: try every
   possible boundary t (everything shorter than t - the higher-frequency
   tone - in one cluster, everything at or beyond it in the other) and
   keep the one that maximizes the variance *between* the two clusters'
   means, weighted by their populations. That's a globally optimal split
   for this histogram (unlike a naively-initialized iterative 2-means,
   which can converge on a lopsided local optimum when, as here, the two
   populations are close together and roughly balanced). Returns the
   boundary length. */
static long WAV_HistThreshold(const unsigned long *hist)
{
	double total_count = 0, total_sum = 0;
	double count_below = 0, sum_below = 0;
	double best_variance = -1;
	long i, best_t = 1;

	for (i = 0; i < WAV_HIST_SIZE; i++) {
		total_count += hist[i];
		total_sum += (double) i * hist[i];
	}
	if (total_count == 0)
		return 1;

	for (i = 1; i < WAV_HIST_SIZE; i++) {
		double count_above, sum_above, mean_below, mean_above, diff, variance;
		count_below += hist[i - 1];
		sum_below += (double) (i - 1) * hist[i - 1];
		count_above = total_count - count_below;
		if (count_below == 0 || count_above == 0)
			continue;
		sum_above = total_sum - sum_below;
		mean_below = sum_below / count_below;
		mean_above = sum_above / count_above;
		diff = mean_above - mean_below;
		variance = count_below * count_above * diff * diff;
		if (variance > best_variance) {
			best_variance = variance;
			best_t = i;
		}
	}
	return best_t;
}

/* Half-cycles kept in the sliding calibration window (see WAV_ClassifyCtx
   below): wide enough to give WAV_HistThreshold() a statistically stable
   split, narrow enough to track real changes in the recording's character
   (e.g. a standard-speed leader tone giving way to turbo-speed data) -
   roughly 0.3-0.6s of audio at typical tape carrier rates. */
enum { WAV_WINDOW_SIZE = 4000 };

/* How often (in half-cycles) to recompute the threshold from the window.
   An Otsu search over WAV_HIST_SIZE buckets is cheap, so this is just to
   avoid redoing it on literally every single half-cycle. */
enum { WAV_RECALIBRATE_EVERY = 64 };

/* Called once per merged same-tone run (see WAV_Classify() below), with
   its logical level (1 = MARK/high-frequency tone, 0 = SPACE/low-frequency
   tone) and duration in samples. Returns FALSE to abort. */
typedef int (*WAV_ToneRunFn)(void *ctx, int level, long duration_samples);

typedef struct {
	WAV_ToneRunFn on_run;
	void *on_run_ctx;
	long threshold;
	int tone; /* -1 = none merged yet, 0 = short (high-frequency) tone, 1 = long (low-frequency) tone */
	long merged_samples;
	/* Sliding-window calibration state: HIST is the histogram of the last
	   (up to) WAV_WINDOW_SIZE smoothed half-cycle lengths in WINDOW (a
	   circular buffer); THRESHOLD is refreshed from it periodically. Real
	   tape recordings drift in character over their length - a single
	   recording can carry a standard-speed leader/header followed by a
	   turbo-speed payload with different tone characteristics - so a
	   single whole-file threshold (this file's very first approach) either
	   fits the (usually much larger) payload and misclassifies the leader,
	   or vice versa. Recalibrating from a trailing window instead tracks
	   that drift, the way a real FSK demodulator's timing/level recovery
	   does. */
	unsigned long hist[WAV_HIST_SIZE];
	long window[WAV_WINDOW_SIZE];
	int window_count;
	int window_pos;
	int since_recalibrate;
} WAV_ClassifyCtx;

/* Classifies each half-cycle against the (adaptively calibrated) threshold
   and merges consecutive same-tone runs, calling ON_RUN() - once per
   merged run, not per half-cycle. This is the demodulation step: it turns
   many carrier half-cycles of the same tone into the single bit-aligned
   level change a real demodulator would have produced. */
static int WAV_Classify(void *ctx_, long run_samples, long smoothed_samples)
{
	WAV_ClassifyCtx *ctx = (WAV_ClassifyCtx *) ctx_;
	int tone = smoothed_samples < ctx->threshold ? 0 : 1;
	long idx;

	if (ctx->tone == -1) {
		ctx->tone = tone;
		ctx->merged_samples = run_samples;
	}
	else if (tone != ctx->tone) {
		/* tone 0 (short half-cycle/high-frequency) is the MARK convention
		   (logical 1); tone 1 (long/low-frequency) is SPACE (logical 0). */
		if (!ctx->on_run(ctx->on_run_ctx, ctx->tone == 0 ? 1 : 0, ctx->merged_samples))
			return FALSE;
		ctx->tone = tone;
		ctx->merged_samples = run_samples;
	}
	else
		ctx->merged_samples += run_samples;

	/* Feed this half-cycle into the trailing calibration window, evicting
	   the oldest entry once it's full. */
	idx = smoothed_samples < 0 ? 0 : smoothed_samples;
	if (idx >= WAV_HIST_SIZE)
		idx = WAV_HIST_SIZE - 1;
	if (ctx->window_count == WAV_WINDOW_SIZE)
		ctx->hist[ctx->window[ctx->window_pos]]--;
	else
		ctx->window_count++;
	ctx->window[ctx->window_pos] = idx;
	ctx->hist[idx]++;
	ctx->window_pos = (ctx->window_pos + 1) % WAV_WINDOW_SIZE;

	if (++ctx->since_recalibrate >= WAV_RECALIBRATE_EVERY) {
		ctx->since_recalibrate = 0;
		ctx->threshold = WAV_HistThreshold(ctx->hist);
	}
	return TRUE;
}

/* --- Bootstrap byte decode ----------------------------------------------
   The OS's own byte-receive interrupt (see cassette.c's CassetteRead(),
   which only signals a loaded byte - and so only fires the interrupt that
   delivers it to the running program - for events where IMG_TAPE_Read()
   reports is_gap==FALSE) never fires for "wavp"/"fsk " blocks: those
   always report is_gap=TRUE, by design, since they represent continuous
   signal meant to be read via a custom loader's own direct POKEY_SKSTAT
   polling (exactly how the game's own turbo routine reads it, once it has
   taken over) - not the OS's interrupt-driven byte assembly. That means a
   WAV recording's initial standard-speed bootstrap segment, if the whole
   file is emitted as one continuous "wavp" signal, can never be read by
   the stock OS, no matter how faithfully that segment's signal is
   reproduced: the interrupt that would ever deliver a byte from it simply
   never fires.

   So before falling back to "wavp", this looks for a run of standard
   600-baud-framed bytes at the start of the recording (10 bits/byte:
   start=0, 8 data bits LSB-first, stop=1 - the same convention the
   existing non-fsk IMG_TAPE_SerinStatus() branch already uses), decoded
   directly from the merged tone-level runs via a simple bit-center
   sampler, and emits them as a genuine "data" chunk the stock reader can
   consume the normal way. Decoding stops at the first framing error (a
   bad start or stop bit) - taken as the point where turbo-speed data
   begins - and the rest of the file continues as "wavp" from there. */

typedef struct {
	int level;     /* 1 = MARK, 0 = SPACE */
	long duration; /* run length, in samples */
} WAV_ToneRun;

typedef struct {
	WAV_ToneRun *runs;
	long count;
	long capacity;
} WAV_CollectCtx;

static int WAV_CollectRun(void *ctx_, int level, long duration_samples)
{
	WAV_CollectCtx *ctx = (WAV_CollectCtx *) ctx_;
	if (ctx->count == ctx->capacity) {
		long new_cap = ctx->capacity ? ctx->capacity * 2 : 4096;
		ctx->runs = (WAV_ToneRun *) Util_realloc(ctx->runs, new_cap * sizeof(WAV_ToneRun));
		ctx->capacity = new_cap;
	}
	ctx->runs[ctx->count].level = level;
	ctx->runs[ctx->count].duration = duration_samples;
	ctx->count++;
	return TRUE;
}

/* A position within the collected run array: RUN_IDX identifies the run,
   OFFSET is how many samples into it. Advances strictly forward. */
typedef struct {
	long run_idx;
	long offset;
} WAV_Cursor;

/* Moves CUR forward by NUM_SAMPLES and returns the level at the new
   position, or -1 if that runs past the end of the collected runs. */
static int WAV_CursorAdvance(const WAV_ToneRun *runs, long count, WAV_Cursor *cur, long num_samples)
{
	cur->offset += num_samples;
	while (cur->run_idx < count && cur->offset >= runs[cur->run_idx].duration) {
		cur->offset -= runs[cur->run_idx].duration;
		cur->run_idx++;
	}
	if (cur->run_idx >= count)
		return -1;
	return runs[cur->run_idx].level;
}

/* Attempts to decode one UART byte with CUR positioned at what should be
   the very start of its start bit. On success returns the byte (0-255)
   and leaves *cur at the start of the next byte's potential start bit; on
   a framing error returns -1 and leaves *cur unspecified. */
static int WAV_TryDecodeByte(const WAV_ToneRun *runs, long count, WAV_Cursor *cur, double bit_samples)
{
	WAV_Cursor c = *cur;
	long step = (long) (bit_samples + 0.5);
	long half = step / 2;
	int byte = 0, bit, level;

	level = WAV_CursorAdvance(runs, count, &c, half); /* center of the start bit */
	if (level != 0)
		return -1;
	for (bit = 0; bit < 8; bit++) {
		level = WAV_CursorAdvance(runs, count, &c, step); /* center of the next data bit */
		if (level < 0)
			return -1;
		if (level)
			byte |= 1 << bit;
	}
	level = WAV_CursorAdvance(runs, count, &c, step); /* center of the stop bit */
	if (level != 1)
		return -1;
	WAV_CursorAdvance(runs, count, &c, step - half); /* end of the stop bit */
	*cur = c;
	return byte;
}

/* Fewest decoded bytes required before accepting a candidate start-bit
   position - guards against a short lucky coincidence in noise/turbo data
   being mistaken for the start of a real standard-speed record. Combined
   with the sync-byte check and checksum validation below (a real Atari
   tape record always starts with two $55 sync bytes, written by the OS's
   own CSAVE routine, and always ends with a trailing checksum byte - see
   WAV_ChecksumValid()), this makes a false accept vanishingly unlikely. */
enum { WAV_MIN_BOOTSTRAP_BYTES = 8 };

/* Validates BYTES[0..N-1] as a real Atari SIO record: the last byte must
   equal the checksum (sum-with-end-around-carry, matching sio.c's own
   SIO_ChkSum()) of every byte before it. Requires at least 4 bytes (2 sync
   + at least 1 content byte + the checksum itself) - shorter than that
   isn't a meaningful checksum claim. */
static int WAV_ChecksumValid(const UBYTE *bytes, long n)
{
	long i;
	unsigned int sum = 0;
	if (n < 4)
		return FALSE;
	for (i = 0; i < n - 1; i++) {
		sum += bytes[i];
		do {
			sum = (sum & 0xff) + (sum >> 8);
		} while (sum > 255);
	}
	return sum == bytes[n - 1];
}

/* Flags a candidate whose content bytes (everything between the 2 sync
   bytes + control byte and the trailing checksum byte) are all identical -
   most commonly all zero. Tracing an actual boot found exactly this: a
   flat, silent stretch between two real records decoded as a spurious but
   checksum-*valid* all-zero "record", because for an all-identical-byte
   payload the checksum is entirely determined by the sync+control bytes
   alone (every content byte contributes the same fixed amount) - so
   WAV_ChecksumValid() alone can't tell a genuine short all-zero record
   from a long flat/silent run that happens to produce the right control
   byte by chance. A real record's content is executable code or varied
   data and essentially never uses a single repeated byte value throughout.
   WAV_DecodeBootstrap() below only *prefers* a varied-content candidate
   over one that fails this check, rather than rejecting the latter
   outright - deprioritizing it is enough to stop it from short-circuiting
   the search before a real record elsewhere gets a chance to be tried
   (that's what actually happened - the all-zero stretch always sorted
   first because any start position within it decodes equally "validly"),
   while still allowing it as a last resort if nothing else in the search
   range validates at all. */
static int WAV_HasVariedContent(const UBYTE *bytes, long n)
{
	long i;
	if (n < 5)
		return TRUE;
	for (i = 4; i < n - 1; i++) {
		if (bytes[i] != bytes[3])
			return TRUE;
	}
	return FALSE;
}

/* A real 132-byte boot record ($00 flag, 1-sector count, $0380 load address,
   $E456/CIOV init address) belonging to the shared first-stage loader that
   several different TurboSoft-built releases turn out to use verbatim -
   confirmed identical, byte for byte, in two independently-sourced real CAS
   captures (Atarimania's "Turbo Tenis.cas", and a separate "ts1992.cas"
   compilation tape). That loader's *second* record (WAV_KNOWN_LOADER_BODY_SIG
   below) is exactly what several of this project's own WAV captures decode
   as their very first bootstrap record - meaning those recordings are
   missing this real record 1, most likely clipped from the front of the
   original tape rip (see the two constants' use in WAV_ConvertToCAS()). */
static const UBYTE WAV_KNOWN_LOADER_RECORD1[132] = {
	0x55, 0x55, 0xfa, 0x00, 0x01, 0x80, 0x03, 0x56, 0xe4, 0xa2, 0x3b, 0x9a, 0x38, 0xbd, 0x99, 0x03,
	0xa8, 0xed, 0xff, 0x03, 0x8c, 0xff, 0x03, 0x48, 0xca, 0x10, 0xf1, 0x60, 0x6e, 0x6d, 0x6c, 0xca,
	0xc9, 0x3b, 0xf7, 0xf5, 0x2b, 0x9d, 0x6e, 0x6c, 0xde, 0xde, 0x0a, 0x84, 0x43, 0xa1, 0xb5, 0x2f,
	0x1b, 0x6e, 0x5f, 0x8d, 0x64, 0x54, 0x84, 0x8f, 0xe9, 0xd5, 0x05, 0x10, 0x63, 0x54, 0x82, 0x59,
	0x49, 0x59, 0x60, 0xbe, 0xb3, 0xf6, 0x21, 0x1e, 0x81, 0x81, 0x7e, 0xb4, 0xa4, 0xad, 0x8d, 0x34,
	0x50, 0xae, 0xaf, 0x15, 0xa9, 0xa5, 0xa2, 0x3a, 0x60, 0x00, 0x52, 0x40, 0x00, 0x20, 0x23, 0x00,
	0x3a, 0x02, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x01, 0x54
};
enum { WAV_KNOWN_LOADER_RECORD1_BAUD = 619 };
enum { WAV_KNOWN_LOADER_RECORD1_GAP = 4003 };

/* The first 16 bytes (2 sync + control + 13 payload) of that same shared
   loader's second record - the record this file's own comparator actually
   finds and decodes as "segment 0" when record 1 above wasn't captured.
   Matched against a real decode's own first bytes in
   WAV_LooksLikeLoaderBodyMissingRecord1() below. */
static const UBYTE WAV_KNOWN_LOADER_BODY_SIG[16] = {
	0x55, 0x55, 0xfc, 0x01, 0x00, 0x70, 0x56, 0xe4, 0xa9, 0x00, 0xa0, 0x02, 0x91, 0x58, 0x8d, 0xc6
};

/* TRUE if BYTES/N is this specific shared loader's second record - i.e. a
   valid decode that nonetheless can't be a real record 1 itself (a genuine
   one always starts with a $00 flag byte right after its control byte; this
   one's equivalent position is $01 - see WAV_KNOWN_LOADER_RECORD1 above). */
static int WAV_LooksLikeLoaderBodyMissingRecord1(const UBYTE *bytes, long n)
{
	return n >= (long) sizeof(WAV_KNOWN_LOADER_BODY_SIG)
	       && memcmp(bytes, WAV_KNOWN_LOADER_BODY_SIG, sizeof(WAV_KNOWN_LOADER_BODY_SIG)) == 0;
}

/* Longest bootstrap this will decode. Real boot loaders are typically a
   few hundred bytes; this is a generous ceiling, not a tuned expectation. */
enum { WAV_MAX_BOOTSTRAP_BYTES = 4096 };

/* How many candidate start-bit positions (successive runs) to try before
   concluding there's no standard-speed bootstrap at all. Used for the very
   first search (from the start of the file, where the leader tone before
   the bootstrap can legitimately be several seconds long). */
enum { WAV_MAX_START_CANDIDATES = 8000 };

/* Same, but for searching right after an already-decoded segment for a
   FOLLOWING one (real boot loaders are often more than one standard-speed
   record). Deliberately much smaller: a genuine next record starts within
   a short inter-record gap, so a small budget finds it fast, while keeping
   the search from wandering deep into what is actually turbo-speed data
   and mistaking a coincidental short match there for a real record. */
enum { WAV_BOOTSTRAP_CONTINUATION_CANDIDATES = 10000 };

/* Upper bound on how many standard-speed segments WAV_ConvertToCAS() will
   chain together, purely as a safety net against pathological input (a
   real tape can genuinely have a few dozen - see the comment where this is
   used). */
enum { WAV_MAX_BOOTSTRAP_SEGMENTS = 256 };

/* A "standard-speed" bootstrap isn't necessarily literally 600 baud - real
   tape decks and turbo-loader bootstraps commonly run a bit off that (this
   file's own tests found one at ~802 baud) - so this tries a range of
   candidate rates, not just DEFAULT_BAUDRATE, and keeps whichever one
   decodes the longest valid (sync-prefixed) run. Bounds are generous
   enough to cover plausible "mildly accelerated leader" rates without
   reaching into genuine turbo territory (where this whole approach
   doesn't apply - see the big comment above). */
enum { WAV_BOOTSTRAP_MIN_BAUD_X10 = 5500 };  /* 550.0 baud */
enum { WAV_BOOTSTRAP_MAX_BAUD_X10 = 9000 };  /* 900.0 baud */
enum { WAV_BOOTSTRAP_BAUD_STEP_X10 = 20 };   /*   2.0 baud */

/* Stop the baud sweep early once a candidate decodes at least this many
   bytes - already far too long to be a coincidence, and refining the rate
   further isn't worth the extra search time. */
enum { WAV_BOOTSTRAP_GOOD_ENOUGH_BYTES = 32 };

/* Searches RUNS[START_RUN_IDX..COUNT) across a range of candidate baud
   rates for the start of a standard-speed byte stream, and decodes as many
   contiguous bytes as keep validating. Tries at most MAX_TRIES candidate
   start-bit positions per baud rate. Sets *OUT_BYTES and *OUT_NUM_BYTES to a
   malloc'd buffer (caller frees), *OUT_BAUD to the winning rate, and
   *OUT_BOUNDARY to the position right after the last decoded byte - or
   *out_num_bytes to 0 (*OUT_BYTES NULL, *OUT_BOUNDARY zeroed) if no
   bootstrap was found at any candidate rate. Also sets *OUT_LEAD_SAMPLES to
   the sample count skipped before the accepted start bit, and
   *OUT_LEAD_RUN_IDX to the run index the accepted candidate started at
   (both measured from START_RUN_IDX) - the caller uses these to preserve
   the actual lead-in audio (see the big comment at the call site on why a
   bare gap duration isn't enough). */
static void WAV_DecodeBootstrap(const WAV_ToneRun *runs, long count, long start_run_idx,
                                 long max_tries, int sample_rate,
                                 UBYTE **out_bytes, long *out_num_bytes, int *out_baud,
                                 WAV_Cursor *out_boundary, long *out_lead_samples,
                                 long *out_lead_run_idx)
{
	long baud_x10;
	UBYTE *best_bytes = NULL;
	long best_num_bytes = 0, best_lead_samples = 0, best_lead_run_idx = start_run_idx;
	int best_baud = 0;
	WAV_Cursor best_boundary;
	/* A record with no varied content (see WAV_HasVariedContent()) is kept
	   here instead, so it can't short-circuit the search (via the "good
	   enough" exit below) before a shorter but varied-content candidate
	   elsewhere gets a chance to be tried - but it's still used as a last
	   resort if nothing else validates at all, since a genuine record with
	   uniform content is possible in principle, just not preferred over a
	   real one when both are on the table. */
	UBYTE *fallback_bytes = NULL;
	long fallback_num_bytes = 0, fallback_lead_samples = 0, fallback_lead_run_idx = start_run_idx;
	int fallback_baud = 0;
	WAV_Cursor fallback_boundary;

	best_boundary.run_idx = start_run_idx;
	best_boundary.offset = 0;
	fallback_boundary.run_idx = start_run_idx;
	fallback_boundary.offset = 0;

	for (baud_x10 = WAV_BOOTSTRAP_MIN_BAUD_X10; baud_x10 <= WAV_BOOTSTRAP_MAX_BAUD_X10;
	     baud_x10 += WAV_BOOTSTRAP_BAUD_STEP_X10) {
		double bit_samples = (double) sample_rate * 10.0 / baud_x10;
		WAV_Cursor search;
		long lead_samples = 0;
		long tries;

		search.run_idx = start_run_idx;
		search.offset = 0;
		for (tries = 0; tries < max_tries && search.run_idx < count; tries++) {
			if (runs[search.run_idx].level == 0) {
				WAV_Cursor cur = search;
				UBYTE *bytes = NULL;
				long n = 0, cap = 0;
				for (;;) {
					int b = WAV_TryDecodeByte(runs, count, &cur, bit_samples);
					if (b < 0)
						break;
					if (n == cap) {
						cap = cap ? cap * 2 : 256;
						bytes = (UBYTE *) Util_realloc(bytes, cap);
					}
					bytes[n++] = (UBYTE) b;
					if (n >= WAV_MAX_BOOTSTRAP_BYTES)
						break;
				}
				if (n >= WAV_MIN_BOOTSTRAP_BYTES && bytes[0] == 0x55 && bytes[1] == 0x55
				    && WAV_ChecksumValid(bytes, n)) {
					if (WAV_HasVariedContent(bytes, n)) {
						if (n > best_num_bytes) {
							free(best_bytes);
							best_bytes = bytes;
							best_num_bytes = n;
							best_baud = (int) (baud_x10 / 10);
							best_boundary = cur;
							best_lead_samples = lead_samples;
							best_lead_run_idx = search.run_idx;
							bytes = NULL;
						}
						if (best_num_bytes >= WAV_BOOTSTRAP_GOOD_ENOUGH_BYTES) {
							free(bytes);
							goto done;
						}
					}
					else if (n > fallback_num_bytes) {
						free(fallback_bytes);
						fallback_bytes = bytes;
						fallback_num_bytes = n;
						fallback_baud = (int) (baud_x10 / 10);
						fallback_boundary = cur;
						fallback_lead_samples = lead_samples;
						fallback_lead_run_idx = search.run_idx;
						bytes = NULL;
					}
				}
				free(bytes);
			}
			lead_samples += runs[search.run_idx].duration;
			search.run_idx++;
		}
	}
done:
	if (best_num_bytes > 0) {
		free(fallback_bytes);
		*out_bytes = best_bytes;
		*out_num_bytes = best_num_bytes;
		*out_baud = best_baud;
		*out_boundary = best_boundary;
		*out_lead_samples = best_lead_samples;
		*out_lead_run_idx = best_lead_run_idx;
	}
	else {
		*out_bytes = fallback_bytes;
		*out_num_bytes = fallback_num_bytes;
		*out_baud = fallback_baud;
		*out_boundary = fallback_boundary;
		*out_lead_samples = fallback_lead_samples;
		*out_lead_run_idx = fallback_lead_run_idx;
	}
}

/* Shared by WAV_ConvertToCAS() and CAS_RecoverBootstrapFromFSK(): given a
   demodulated run array (RUNS/COUNT - already classified into MARK/SPACE
   stretches, whether by the WAV comparator+classifier or read directly
   from a CAS "fsk " chunk's own pulse pairs) at SAMPLE_RATE, writes a full
   synthetic CAS stream to OUT: FUJI header, any standard-speed bootstrap
   segments found (decoded into real "data" chunks - see
   WAV_DecodeBootstrap()), and the remaining signal as "wavp" pulses.
   Takes ownership of RUNS (frees it, on both success and failure). */
static int WAV_EncodeBootstrapRecovery(WAV_ToneRun *runs, long count, int sample_rate, FILE *out)
{
	WAV_ChunkWriter writer;
	CAS_Header header;
	UBYTE *bootstrap_bytes;
	long bootstrap_num_bytes, bootstrap_lead_samples;
	int bootstrap_baud;
	WAV_Cursor boundary;
	int ok;
	long i;

	/* Header: FUJI marker (empty description) + baud (matches the
	   bootstrap "data" chunk, if any; unused by "wavp"). */
	memset(&header, 0, sizeof(header));
	if (fwrite("FUJI", 1, 4, out) != 4 || fwrite(&header.length_lo, 1, 4, out) != 4)
		goto fail;
	header.aux_lo = DEFAULT_BAUDRATE & 0xFF;
	header.aux_hi = DEFAULT_BAUDRATE >> 8;
	if (fwrite("baud", 1, 4, out) != 4 || fwrite(&header.length_lo, 1, 4, out) != 4)
		goto fail;

	/* Look for standard-speed (UART-framed) records throughout the file and
	   write each one as a real "data" chunk (see the big comment above), at
	   whatever baud rate it actually decoded at (a real recording's records
	   aren't necessarily exactly DEFAULT_BAUDRATE). This isn't just a short
	   boot header: tracing an actual boot against this file showed the OS
	   successfully reading and checksum-validating a real 132-byte record,
	   then a second one - at which point it tries to read a THIRD record
	   the same way, which can never succeed against un-decoded "wavp"
	   signal (see the big comment above) and times out. Checking further
	   confirmed a decodable, checksum-valid record immediately follows the
	   second one too - and another after that, for dozens of records in a
	   row, all the same length and all validating against the real Atari
	   SIO checksum (WAV_ChecksumValid() below) - so this keeps decoding
	   consecutive records for as long as they're found, not just the first
	   one or two. (An earlier version of this stopped at a record whose
	   control byte was $FE, reasoning that's the standard Atari cassette
	   EOF marker - see the raw-binary-format branch of ReadNextRecord()
	   above, which writes that exact convention. That reasoning doesn't
	   hold here: plenty of further checksum-valid records follow such a
	   record in this file, with an entirely different, non-standard
	   control byte of their own - evidently this tape's own protocol
	   doesn't treat $FE as an end-of-data signal the way CLOAD-style files
	   do, so an EOF-shaped record is just accepted like any other.) This
	   stops once no further record validates - real turbo-speed signal
	   with a different structure has begun - or the safety cap is hit;
	   WAV_ChecksumValid() (required for every accepted record, not just a
	   sync-and-length heuristic) is what keeps a long, generously-budgeted
	   search like this from mistaking a coincidental short match deep in
	   that turbo-speed signal for a real record.

	   Between records, the audio isn't silence - tracing a boot showed the
	   OS's leader-tone detector ($ED3D) measuring the actual *period* of
	   toggling on the tape to reconfirm sync before each record, and timing
	   straight out to a device-timeout status if that toggling never
	   arrives. An earlier version of this collapsed each inter-record gap
	   into a single bare "wait GAP ms, then read" header field on the
	   following "data" chunk - which reads as a flat, unchanging level for
	   that whole span (see IMG_TAPE_SerinStatus()'s "data" branch: no
	   bytes consumed yet reads as pure MARK), giving $ED3D nothing to
	   measure and reproducing exactly that timeout. So instead, the actual
	   lead-in audio between records is preserved as real "wavp" pulses
	   (interleaved with the "data" chunks below via repeated WAV_ChunkWriter
	   flushes) - whatever genuine leader tone or other signal is really
	   there on the tape, rather than a synthetic flat gap standing in for
	   it. */
	WAV_ChunkWriterInit(&writer, out);
	ok = TRUE;
	boundary.run_idx = 0;
	boundary.offset = 0;
	{
		int segment;
		for (segment = 0; ok && segment < WAV_MAX_BOOTSTRAP_SEGMENTS; segment++) {
			long search_from = boundary.run_idx;
			long search_from_offset = boundary.offset;
			long max_tries = segment == 0 ? WAV_MAX_START_CANDIDATES
			                               : WAV_BOOTSTRAP_CONTINUATION_CANDIDATES;
			long lead_run_idx;
			CAS_Header data_header;

			WAV_DecodeBootstrap(runs, count, search_from, max_tries,
			                     sample_rate, &bootstrap_bytes, &bootstrap_num_bytes,
			                     &bootstrap_baud, &boundary, &bootstrap_lead_samples,
			                     &lead_run_idx);
			if (bootstrap_num_bytes <= 0) {
				boundary.run_idx = search_from;
				boundary.offset = search_from_offset;
				break;
			}

			/* This tape's actual first bootstrap record is the shared
			   loader's second record, not its first (see
			   WAV_LooksLikeLoaderBodyMissingRecord1() above) - the real
			   record 1 was not captured, most likely clipped from the
			   front of the tape rip. Recover it by writing the known-good
			   record 1 as its own "data" chunk before anything else, so
			   the OS's boot-record reader sees a real, valid record 1
			   first, exactly as it would from a complete capture. */
			if (segment == 0 && WAV_LooksLikeLoaderBodyMissingRecord1(bootstrap_bytes, bootstrap_num_bytes)) {
				CAS_Header record1_header;
				memset(&record1_header, 0, sizeof(record1_header));
				record1_header.aux_lo = WAV_KNOWN_LOADER_RECORD1_BAUD & 0xFF;
				record1_header.aux_hi = (WAV_KNOWN_LOADER_RECORD1_BAUD >> 8) & 0xFF;
				if (fwrite("baud", 1, 4, out) != 4 || fwrite(&record1_header.length_lo, 1, 4, out) != 4) {
					free(bootstrap_bytes);
					ok = FALSE;
					break;
				}
				memcpy(record1_header.identifier, "data", 4);
				record1_header.length_lo = sizeof(WAV_KNOWN_LOADER_RECORD1) & 0xFF;
				record1_header.length_hi = (sizeof(WAV_KNOWN_LOADER_RECORD1) >> 8) & 0xFF;
				/* Real captures of this same record (see the constants'
				   own comment above) precede it with a multi-second gap -
				   the leader tone before the very first record on a real
				   tape, which this recording's own missing record 1 took
				   with it. Without some such gap here, the OS's own boot
				   dispatcher doesn't get the settling time it expects
				   before the first record and the read fails - confirmed
				   directly by testing both ways. WAV_KNOWN_LOADER_RECORD1_GAP
				   is the value one such real capture ("ts1992.cas") itself
				   used for this exact record. */
				record1_header.aux_lo = WAV_KNOWN_LOADER_RECORD1_GAP & 0xFF;
				record1_header.aux_hi = (WAV_KNOWN_LOADER_RECORD1_GAP >> 8) & 0xFF;
				if (fwrite(&record1_header, 1, 8, out) != 8
				    || fwrite(WAV_KNOWN_LOADER_RECORD1, 1, sizeof(WAV_KNOWN_LOADER_RECORD1), out)
				       != sizeof(WAV_KNOWN_LOADER_RECORD1)) {
					free(bootstrap_bytes);
					ok = FALSE;
					break;
				}
			}

			/* Preserve the real lead-in audio as "wavp" pulses (see the big
			   comment above), priming with a 1-sample throwaway pulse when
			   the true starting level is MARK, same as "wavp"'s own
			   parity-based level convention requires at the start of any
			   chunk (see IMG_TAPE_SerinStatus()'s "wavp" branch). */
			if (lead_run_idx > search_from) {
				int true_level = runs[search_from].level;
				long first_remaining = runs[search_from].duration - search_from_offset;
				if (true_level != 0)
					ok = WAV_EmitRun(&writer, 1, sample_rate);
				if (ok && first_remaining > 0)
					ok = WAV_EmitRun(&writer, first_remaining, sample_rate);
				for (i = search_from + 1; ok && i < lead_run_idx; i++)
					ok = WAV_EmitRun(&writer, runs[i].duration, sample_rate);
				if (ok)
					ok = WAV_ChunkWriterFlush(&writer);
			}
			if (!ok) {
				free(bootstrap_bytes);
				break;
			}

			memset(&data_header, 0, sizeof(data_header));
			data_header.aux_lo = bootstrap_baud & 0xFF;
			data_header.aux_hi = (bootstrap_baud >> 8) & 0xFF;
			if (fwrite("baud", 1, 4, out) != 4 || fwrite(&data_header.length_lo, 1, 4, out) != 4) {
				free(bootstrap_bytes);
				ok = FALSE;
				break;
			}
			memcpy(data_header.identifier, "data", 4);
			data_header.length_lo = bootstrap_num_bytes & 0xFF;
			data_header.length_hi = (bootstrap_num_bytes >> 8) & 0xFF;
			data_header.aux_lo = 0; /* the gap is now real "wavp" pulses, above */
			data_header.aux_hi = 0;
			if (fwrite(&data_header, 1, 8, out) != 8
			    || fwrite(bootstrap_bytes, 1, bootstrap_num_bytes, out) != (size_t) bootstrap_num_bytes) {
				free(bootstrap_bytes);
				ok = FALSE;
				break;
			}
			free(bootstrap_bytes);
			bootstrap_bytes = NULL;
		}
	}
	if (!ok)
		goto fail;
	/* Restore the default rate for anything after the last decoded record
	   (moot for "wavp", which ignores it, but keeps block_baudrates[]
	   consistent for any future reader/tooling that inspects it). Written
	   even when no bootstrap segment was found at all, since it's cheap
	   and harmless, and keeps this unconditional on the loop above. */
	{
		CAS_Header data_header;
		memset(&data_header, 0, sizeof(data_header));
		data_header.aux_lo = DEFAULT_BAUDRATE & 0xFF;
		data_header.aux_hi = DEFAULT_BAUDRATE >> 8;
		if (fwrite("baud", 1, 4, out) != 4 || fwrite(&data_header.length_lo, 1, 4, out) != 4)
			goto fail;
	}

	/* Emit whatever's left (from the last decode boundary, or from the very
	   start if no bootstrap was found at all) as "wavp" pulses - same
	   priming-for-parity logic as each inter-record lead-in above. */
	if (boundary.run_idx < count) {
		int true_level = runs[boundary.run_idx].level;
		long remaining = runs[boundary.run_idx].duration - boundary.offset;
		if (true_level != 0)
			ok = WAV_EmitRun(&writer, 1, sample_rate);
		if (ok && remaining > 0)
			ok = WAV_EmitRun(&writer, remaining, sample_rate);
		for (i = boundary.run_idx + 1; ok && i < count; i++)
			ok = WAV_EmitRun(&writer, runs[i].duration, sample_rate);
	}
	if (ok)
		ok = WAV_ChunkWriterFlush(&writer);
	if (!ok)
		goto fail;

	free(runs);
	return TRUE;
fail:
	free(runs);
	return FALSE;
}

/* Converts the PCM data at DATA_OFFSET/DATA_SIZE in F into a synthetic CAS
   byte stream, written to OUT: demodulates the carrier into tone runs (see
   WAV_ScanHalfCycles()/WAV_Classify()) and hands them to
   WAV_EncodeBootstrapRecovery() to do the actual encoding. */
static int WAV_ConvertToCAS(FILE *f, int channels, int sample_rate, int bits_per_sample,
                             long data_offset, long data_size, FILE *out)
{
	int bytes_per_sample = bits_per_sample / 8;
	int frame_size = bytes_per_sample * channels;
	long num_frames = data_size / frame_size;
	UBYTE *block;
	long peak = 1; /* avoid a zero-width hysteresis band on silence */
	int hi_thresh, lo_thresh;
	long frames_left;
	WAV_ClassifyCtx *classify_ctx;
	WAV_CollectCtx collect_ctx;
	int ok;

	if (num_frames <= 0 || frame_size <= 0)
		return FALSE;
	block = (UBYTE *) Util_malloc(WAV_READ_FRAMES * frame_size);
	/* WAV_ClassifyCtx holds a several-KB sliding window - heap-allocate it
	   rather than risk a large stack frame in constrained environments. */
	classify_ctx = (WAV_ClassifyCtx *) Util_malloc(sizeof(WAV_ClassifyCtx));

	/* Pass 1: peak amplitude. */
	if (fseek(f, data_offset, SEEK_SET) != 0) {
		free(block);
		free(classify_ctx);
		return FALSE;
	}
	frames_left = num_frames;
	while (frames_left > 0) {
		long want = frames_left > WAV_READ_FRAMES ? WAV_READ_FRAMES : frames_left;
		long got = (long) fread(block, frame_size, want, f);
		long j;
		if (got <= 0)
			break;
		for (j = 0; j < got; j++) {
			long v = WAV_DownmixFrame(block + j * frame_size, channels, bytes_per_sample);
			long a = v < 0 ? -v : v;
			if (a > peak)
				peak = a;
		}
		frames_left -= got;
	}
	hi_thresh = (int) (peak * WAV_HYSTERESIS_PERCENT / 100);
	lo_thresh = -hi_thresh;

	/* Pass 2: demodulate the whole file - classify each half-cycle against
	   an adaptively calibrated threshold (see WAV_Classify()) and collect
	   one merged run per same-tone stretch. The threshold starts low
	   (biasing early, still-uncalibrated samples to the "long" tone) and
	   self-corrects within the first few dozen half-cycles. */
	memset(classify_ctx, 0, sizeof(*classify_ctx));
	memset(&collect_ctx, 0, sizeof(collect_ctx));
	classify_ctx->on_run = WAV_CollectRun;
	classify_ctx->on_run_ctx = &collect_ctx;
	classify_ctx->threshold = 1;
	classify_ctx->tone = -1;
	ok = WAV_ScanHalfCycles(f, channels, bytes_per_sample, frame_size, data_offset, num_frames,
	                         block, hi_thresh, lo_thresh, WAV_Classify, classify_ctx);
	if (ok && classify_ctx->tone != -1)
		ok = WAV_CollectRun(&collect_ctx, classify_ctx->tone == 0 ? 1 : 0, classify_ctx->merged_samples);
	free(block);
	free(classify_ctx);
	if (!ok) {
		free(collect_ctx.runs);
		return FALSE;
	}

	return WAV_EncodeBootstrapRecovery(collect_ctx.runs, collect_ctx.count, sample_rate, out);
}

/* Detects a WAV file and, if found, converts it into a synthetic in-memory
   CAS file (via tmpfile()) holding the equivalent "fsk " pulse stream.
   Returns the tmpfile (rewound, ready to be parsed as CAS), or NULL if F
   isn't a supported WAV, or on error. Does not close or otherwise disturb
   the position of FILE. */
static FILE *WAV_OpenAsCAS(FILE *file)
{
	char riff_id[4], wave_id[4];
	UBYTE size_bytes[4];
	int channels, sample_rate, bits_per_sample;
	long data_offset, data_size;
	FILE *tmp;

	if (fseek(file, 0, SEEK_SET) != 0
	    || fread(riff_id, 1, 4, file) != 4
	    || memcmp(riff_id, "RIFF", 4) != 0
	    || fread(size_bytes, 1, 4, file) != 4
	    || fread(wave_id, 1, 4, file) != 4
	    || memcmp(wave_id, "WAVE", 4) != 0)
		return NULL;

	if (!WAV_ParseHeader(file, &channels, &sample_rate, &bits_per_sample, &data_offset, &data_size))
		return NULL;

	tmp = tmpfile();
	if (tmp == NULL)
		return NULL;
	if (!WAV_ConvertToCAS(file, channels, sample_rate, bits_per_sample, data_offset, data_size, tmp)) {
		fclose(tmp);
		return NULL;
	}
	rewind(tmp);
	return tmp;
}
/* --- end of WAV support -------------------------------------------------- */

/* Write contents of the file's block buffer to file, as a separate record;
   then empty the buffer.
   Returns TRUE on success or FALSE on write error. */
static int WriteRecord(IMG_TAPE_t *file)
{
	CAS_Header header;
	int result;

	/* on a raw file, saving is denied because it can hold
	    only 1 file and could cause confusion */
	if (!file->isCAS)
		return FALSE;
	/* always append */
	if (fseek(file->file, file->block_offsets[file->num_blocks], SEEK_SET) != 0)
		return FALSE;
	/* write record header */
	memcpy(header.identifier, "data", 4);
	header.length_lo = file->block_length & 0xFF;
	header.length_hi = (file->block_length >> 8) & 0xFF;
	header.aux_lo = file->save_gap & 0xff;
	header.aux_hi = (file->save_gap >> 8) & 0xff;
	if (fwrite(&header, 1, 8, file->file) != 8)
		return FALSE;
	/* Saving is supported only with standard baudrate. */
	file->block_baudrates[file->num_blocks] = DEFAULT_BAUDRATE;
	file->num_blocks++;
	file->block_offsets[file->num_blocks] = file->block_offsets[file->num_blocks - 1] + file->block_length + 8;
	file->current_block = file->num_blocks;
	/* write record */
	result = fwrite(file->buffer, 1, file->block_length, file->file) == file->block_length;
	if (result) {
		file->save_gap = 0;
		file->block_length = 0;
	}
	return result;
}

/* Flush any unwritten data to tape. */
static int CassetteFlush(IMG_TAPE_t *file)
{
	if (file->block_length > 0)
		return WriteRecord(file) && fflush(file->file) == 0;
	return TRUE;
}

/* --- CAS "fsk " bootstrap recovery --------------------------------------
   A real .CAS file can itself be made entirely of "fsk " chunks (raw
   pulse-timing data - see the format comment near the top of this file),
   with no "data" chunk at all - a handful of real historical tape rips
   turn out to be exactly this: evidently produced by an older/simpler
   WAV-to-CAS conversion tool that only ever emitted "fsk ", never
   attempting to also decode any standard-speed bootstrap into a real
   "data" chunk the way WAV_OpenAsCAS() (above) does for a raw WAV capture.
   Such a file has the exact same problem as the WAV files that motivated
   WAV_EncodeBootstrapRecovery(): the OS's own interrupt-driven byte reader
   never fires for "fsk " blocks (see the big comment above that function),
   so a bootstrap that's only ever present as undecoded "fsk " pulses can
   never actually be read by the stock OS.

   The fix reuses WAV_EncodeBootstrapRecovery() directly: a CAS "fsk "
   chunk's pulses are already exactly the demodulated MARK/SPACE tone-run
   data that function expects (see CAS_ReadFSKAsRuns() below) - no audio
   synthesis or comparator step is needed at all, unlike the WAV case. */

/* Reads a run of consecutive "fsk " chunks (with any interleaving "baud"
   marker chunks skipped) starting at the current position of F - which
   must be positioned right at a chunk header - into a newly malloc'd
   WAV_ToneRun array (one run per pulse pair). Each "fsk " chunk's own
   pulses are read as exact 1/10ms durations (the format's own unit - see
   the comment near the top of this file) and, per that same format's own
   convention, alternate starting from SPACE (level 0) at the start of
   EVERY such chunk (matching IMG_TAPE_WriteAdvance()/IMG_TAPE_SerinStatus()'s
   own per-block parity: see how next_blockbyte, which resets to 0 at each
   new block, drives the "~(next_blockbyte / 2) & 1" level in
   IMG_TAPE_SerinStatus()). Stops at the first chunk that's neither
   "fsk " nor "baud", leaving F positioned right at that chunk's own
   header, or at EOF. Returns FALSE only on a genuine read error partway
   through a chunk's declared length (frees any partial RUNS itself and
   sets *OUT_RUNS to NULL and *OUT_COUNT to 0 in that case). */
static int CAS_ReadFSKAsRuns(FILE *f, WAV_ToneRun **out_runs, long *out_count)
{
	WAV_ToneRun *runs = NULL;
	long count = 0, capacity = 0;

	for (;;) {
		CAS_Header h;
		long chunk_start = ftell(f);
		int length;

		if (fread(&h, 1, 8, f) != 8)
			break;
		if (memcmp(h.identifier, "baud", 4) == 0) {
			length = h.length_lo + (h.length_hi << 8);
			if (fseek(f, length, SEEK_CUR) != 0)
				goto fail;
			continue;
		}
		if (memcmp(h.identifier, "fsk ", 4) != 0) {
			fseek(f, chunk_start, SEEK_SET);
			break;
		}
		length = h.length_lo + (h.length_hi << 8);
		{
			int remaining = length;
			int level = 0; /* every "fsk " chunk starts at SPACE */
			while (remaining >= 2) {
				UBYTE b2[2];
				if (fread(b2, 1, 2, f) != 2)
					goto fail;
				if (count == capacity) {
					long new_cap = capacity ? capacity * 2 : 4096;
					runs = (WAV_ToneRun *) Util_realloc(runs, new_cap * sizeof(WAV_ToneRun));
					capacity = new_cap;
				}
				runs[count].level = level;
				runs[count].duration = b2[0] | (b2[1] << 8);
				count++;
				level = !level;
				remaining -= 2;
			}
			if (remaining > 0 && fseek(f, remaining, SEEK_CUR) != 0)
				goto fail;
		}
	}
	*out_runs = runs;
	*out_count = count;
	return TRUE;
fail:
	free(runs);
	*out_runs = NULL;
	*out_count = 0;
	return FALSE;
}

/* Detects a CAS file whose first real content chunk (right after the FUJI
   marker and its description, and past any leading "baud" chunk) is
   "fsk " - i.e. one with no bootstrap decoded into a real "data" chunk at
   all (see the big comment above) - and, if so, converts it on the fly
   into a synthetic in-memory CAS file (via tmpfile()) with any
   standard-speed bootstrap recovered into real "data" chunks, exactly the
   way WAV_OpenAsCAS() does for a raw WAV capture (just skipping the
   demodulation step, since "fsk " pulses are already the demodulated
   tone-run data - see CAS_ReadFSKAsRuns() above). FILE must be positioned
   right after the initial 6-byte FUJI header, and HEADER holds that
   header's already-read bytes (the same convention ParseCASBody() itself
   uses). A CAS that already starts with a real "data" chunk needs none of
   this - it already works via the normal ParseCASBody() path below - so
   this leaves it untouched. Returns the tmpfile (rewound, ready to be
   parsed as CAS), or NULL if this isn't a pure-"fsk "-first CAS, or on
   error; FILE's original position is restored in either case. */
static FILE *CAS_RecoverBootstrapFromFSK(FILE *file, const CAS_Header *header)
{
	long start_pos = ftell(file);
	UWORD length = header->length_lo | (header->length_hi << 8);
	WAV_ToneRun *runs;
	long count;
	FILE *tmp;
	CAS_Header h;

	/* Skip past the 2 aux bytes + description, exactly as ParseCASBody()
	   does, to reach the first real content chunk. */
	if (fseek(file, 2L + length, SEEK_CUR) != 0)
		goto restore;

	/* Skip any leading "baud" marker chunk(s) to find the first actual
	   content chunk, without consuming it. */
	for (;;) {
		long before = ftell(file);
		if (fread(&h, 1, 8, file) != 8) {
			fseek(file, before, SEEK_SET);
			break;
		}
		if (memcmp(h.identifier, "baud", 4) != 0) {
			fseek(file, before, SEEK_SET);
			break;
		}
		{
			int blen = h.length_lo + (h.length_hi << 8);
			if (fseek(file, blen, SEEK_CUR) != 0)
				goto restore;
		}
	}

	if (fread(&h, 1, 8, file) != 8)
		goto restore;
	if (memcmp(h.identifier, "fsk ", 4) != 0) {
		/* Already starts with "data" (or something else entirely) - not
		   this function's problem to solve. */
		goto restore;
	}
	fseek(file, -8L, SEEK_CUR);

	if (!CAS_ReadFSKAsRuns(file, &runs, &count))
		goto restore;
	if (count == 0) {
		free(runs);
		goto restore;
	}

	tmp = tmpfile();
	if (tmp == NULL) {
		free(runs);
		goto restore;
	}
	/* Virtual sample rate of 10000Hz makes a "sample" exactly 1/10ms -
	   matching the "fsk " pulses' own native unit exactly, so no precision
	   is lost converting them into WAV_EncodeBootstrapRecovery()'s
	   sample-count convention. */
	if (!WAV_EncodeBootstrapRecovery(runs, count, 10000, tmp)) {
		fclose(tmp);
		goto restore;
	}
	rewind(tmp);
	fseek(file, start_pos, SEEK_SET);
	return tmp;
restore:
	fseek(file, start_pos, SEEK_SET);
	return NULL;
}
/* --- end of CAS "fsk " bootstrap recovery -------------------------------- */

/* Parses the CAS chunk stream in IMG->file (already positioned right after
   the initial 6-byte FUJI header, whose already-read bytes are passed in
   HEADER) into IMG's block table. Common tail for both real .CAS files and
   the synthetic CAS stream WAV files are converted into (see
   WAV_OpenAsCAS()), and the fsk-recovered stream CAS_RecoverBootstrapFromFSK()
   produces. Returns FALSE on read error. */
static int ParseCASBody(IMG_TAPE_t *img, CAS_Header *header, char const **description)
{
	UWORD length;
	UWORD skip;
	int blocks;
	int baudrate = DEFAULT_BAUDRATE;

	img->isCAS = TRUE;
	fseek(img->file, 2L, SEEK_CUR);	/* ignore the aux bytes */

	/* read or skip file description */
	skip = length = header->length_lo | (header->length_hi << 8);
	if (length < CASSETTE_DESCRIPTION_MAX)
		skip = 0;
	else
		skip -= CASSETTE_DESCRIPTION_MAX - 1;
	if (length > 0)
		if (fread(img->description, 1, length - skip, img->file) < (length - skip))
			return FALSE;
	img->description[length - skip] = '\0';
	fseek(img->file, skip, SEEK_CUR);

	/* count number of blocks */
	blocks = 0;
	img->block_baudrates[0] = DEFAULT_BAUDRATE;
	img->block_offsets[0] = ftell(img->file);
	for (;;) {
		CAS_Header h;
		/* chunk header is always 8 bytes */
		if (fread(&h, 1, 8, img->file) != 8)
			break;
		length = h.length_lo + (h.length_hi << 8);
		if (h.identifier[0] == 'b' &&
		    h.identifier[1] == 'a' &&
		    h.identifier[2] == 'u' &&
		    h.identifier[3] == 'd') {
			baudrate = h.aux_lo + (h.aux_hi << 8);
			img->block_offsets[blocks] += length + 8;
		}
		else if ((h.identifier[0] == 'd' &&
		          h.identifier[1] == 'a' &&
		          h.identifier[2] == 't' &&
		          h.identifier[3] == 'a') ||
		         (h.identifier[0] == 'f' &&
		          h.identifier[1] == 's' &&
		          h.identifier[2] == 'k' &&
		          h.identifier[3] == ' ') ||
		         (h.identifier[0] == 'w' &&
		          h.identifier[1] == 'a' &&
		          h.identifier[2] == 'v' &&
		          h.identifier[3] == 'p')) {
			img->block_baudrates[blocks] = baudrate;
			if (++blocks >= MAX_BLOCKS) {
				--blocks;
				break;
			}
			img->block_offsets[blocks] = img->block_offsets[blocks - 1] + length + 8;
		}
		/* skip possibly present data block */
		fseek(img->file, length, SEEK_CUR);
	}
	img->num_blocks = blocks;
	*description = img->description;
	return TRUE;
}

IMG_TAPE_t *IMG_TAPE_Open(char const *filename, int *writable, char const **description)
{
	IMG_TAPE_t *img;
	CAS_Header header;
	int got_header;

	img = (IMG_TAPE_t *)Util_malloc(sizeof(IMG_TAPE_t));
	/* Check if the file is writable. If not, recording will be disabled. */
	img->file = fopen(filename, "rb+");
	*writable = img->file != NULL;
	/* If opening for reading+writing failed, reopen it as read-only. */
	if (img->file == NULL)
		img->file = fopen(filename, "rb");
	if (img->file == NULL) {
		free(img);
		return NULL;
	}
	img->description[0] = '\0';

	got_header = fread(&header, 1, 6, img->file) == 6;

	if (got_header
		&& header.identifier[0] == 'F'
		&& header.identifier[1] == 'U'
		&& header.identifier[2] == 'J'
		&& header.identifier[3] == 'I') {
		/* CAS file - but see CAS_RecoverBootstrapFromFSK() for a special
		   case: a CAS whose bootstrap was never decoded into a "data"
		   chunk (several real historical tape rips turn out to be exactly
		   this) is converted on the fly into an equivalent stream with any
		   standard-speed bootstrap recovered, the same way WAV_OpenAsCAS()
		   does for a raw WAV capture below - it's a no-op for any CAS that
		   already starts with a real "data" chunk. */
		FILE *fsk_tmp = CAS_RecoverBootstrapFromFSK(img->file, &header);
		if (fsk_tmp != NULL) {
			CAS_Header new_header;
			if (fread(&new_header, 1, 6, fsk_tmp) != 6) {
				fclose(fsk_tmp);
				fclose(img->file);
				free(img);
				return NULL;
			}
			fclose(img->file);
			img->file = fsk_tmp;
			header = new_header;
			*writable = FALSE; /* fsk-recovered tapes are playback-only */
		}
		if (!ParseCASBody(img, &header, description)) {
			fclose(img->file);
			free(img);
			return NULL;
		}
	}
	else if (got_header
		&& header.identifier[0] == 'R'
		&& header.identifier[1] == 'I'
		&& header.identifier[2] == 'F'
		&& header.identifier[3] == 'F') {
		/* WAV file: converted on the fly (see WAV_OpenAsCAS()) into an
		   equivalent in-memory CAS "wavp" pulse stream, then parsed the
		   same way as any other CAS file. */
		FILE *cas_tmp = WAV_OpenAsCAS(img->file);
		if (cas_tmp == NULL || fread(&header, 1, 6, cas_tmp) != 6) {
			if (cas_tmp != NULL)
				fclose(cas_tmp);
			fclose(img->file);
			free(img);
			return NULL;
		}
		fclose(img->file);
		img->file = cas_tmp;
		if (!ParseCASBody(img, &header, description)) {
			fclose(img->file);
			free(img);
			return NULL;
		}
		*writable = FALSE; /* WAV-derived tapes are playback-only */
	}
	else {
		/* raw file */
		int file_length = Util_flen(img->file);
		img->num_blocks = ((file_length + 127) >> 7) + 1;
		img->isCAS = FALSE;
		*writable = FALSE; /* Writing raw files is not supported */
		*description = NULL;
	}

	img->savetime = 0;
	img->save_gap = 0;
	img->next_blockbyte = 0;
	img->block_length = 0;
	img->current_block = 0;
	img->buffer = (UBYTE *)Util_malloc((img->buffer_size = DEFAULT_BUFFER_SIZE) * sizeof(UBYTE));
	img->was_writing = FALSE;

	return img;
}

void IMG_TAPE_Close(IMG_TAPE_t *file)
{
	if (file->was_writing)
		CassetteFlush(file);
	fclose(file->file);
	free(file->buffer);
	free(file);
}

IMG_TAPE_t *IMG_TAPE_Create(char const *filename, char const *description)
{
	IMG_TAPE_t *img;
	CAS_Header header;
	size_t desc_len;
	FILE *file = NULL;

	/* create new file */
	file = fopen(filename, "wb+");
	if (file == NULL)
		return NULL;

	/* Write the initial FUJI and baud blocks of the CAS file. */
	desc_len = strlen(description);
	memset(&header, 0, sizeof(header));
	/* write CAS-header */
	header.length_lo = (UBYTE) desc_len;
	header.length_hi = (UBYTE) (desc_len >> 8);
	if (fwrite("FUJI", 1, 4, file) != 4
	    || fwrite(&header.length_lo, 1, 4, file) != 4
	    || fwrite(description, 1, desc_len, file) != desc_len) {
		fclose(file);
		return NULL;
	}

	memset(&header, 0, sizeof(header));
	/* All records are written with 600 baud speed. */
	header.aux_lo = DEFAULT_BAUDRATE & 0xff;
	header.aux_hi = DEFAULT_BAUDRATE >> 8;
	if (fwrite("baud", 1, 4, file) != 4
	    || fwrite(&header.length_lo, 1, 4, file) != 4) {
		fclose(file);
		return NULL;
	}

	img = (IMG_TAPE_t *)Util_malloc(sizeof(IMG_TAPE_t));
	img->file = file;
	if (description != NULL)
		Util_strlcpy(img->description, description, CASSETTE_DESCRIPTION_MAX);
	img->isCAS = TRUE;
	img->savetime = 0;
	img->save_gap = 0;
	img->next_blockbyte = 0;
	img->block_length = 0;
	img->current_block = 0;
	img->num_blocks = 0;
	img->block_offsets[0] = strlen(description) + 16;
	img->buffer = (UBYTE *)Util_malloc((img->buffer_size = DEFAULT_BUFFER_SIZE) * sizeof(UBYTE));
	img->was_writing = TRUE;

	return img;
}

/* Enlarge file->buffer to (at least) SIZE if needed. */
static void EnlargeBuffer(IMG_TAPE_t *file, size_t size)
{
	if (file->buffer_size < size) {
		/* Enlarge the buffer at least 2 times. */
		file->buffer_size *= 2;
		if (file->buffer_size < size)
			file->buffer_size = size;
		file->buffer = (UBYTE *)Util_realloc(file->buffer, file->buffer_size * sizeof(UBYTE));
	}
}

/* Read a record from the file. FALSE on error/EOF.
   Writes length of pre-record gap (in ms) into *gap. */
static int ReadNextRecord(IMG_TAPE_t *file, int *gap)
{
	int length;

	/* 0 indicates that there was no previous block being read and
	   current_block already contains the current block number. */
	if (file->block_length != 0) {
		/* Non-zero - a block was being read and it's finished, increase the block number. */
		file->block_length = 0;
		if (++file->current_block >= file->num_blocks)
			/* Last block was already read. */
			return FALSE;
	}

	if (file->isCAS) {
		CAS_Header header;

		if (fseek(file->file, file->block_offsets[file->current_block], SEEK_SET) != 0
		    || fread(&header, 1, 8, file->file) < 8)
			return FALSE;

		/* Determine chunk type - "data", "fsk ", or the WAV-derived "wavp". */
		file->block_is_fsk = header.identifier[0] == 'f' &&
		                     header.identifier[1] == 's' &&
		                     header.identifier[2] == 'k' &&
		                     header.identifier[3] == ' ';
		file->block_is_wav = header.identifier[0] == 'w' &&
		                     header.identifier[1] == 'a' &&
		                     header.identifier[2] == 'v' &&
		                     header.identifier[3] == 'p';

		length = header.length_lo + (header.length_hi << 8);
		*gap = header.aux_lo + (header.aux_hi << 8);
		/* read block into buffer */
		EnlargeBuffer(file, length);
		if (fread(file->buffer, 1, length, file->file) < length)
			return FALSE;
	}
	else {
		file->block_is_fsk = FALSE;
		file->block_is_wav = FALSE;
		length = 132;
		/* Don't enlarge buffer - its default size is at least 132. */
		*gap = (file->current_block == 0 ? 19200 : 260);
		file->buffer[0] = 0x55;
		file->buffer[1] = 0x55;
		if (file->current_block + 1 >= file->num_blocks) {
			/* EOF record */
			file->buffer[2] = 0xfe;
			memset(file->buffer + 3, 0, 128);
		}
		else {
			int bytes;
			if (fseek(file->file, file->current_block * 128, SEEK_SET) != 0
			    || (bytes = fread(file->buffer + 3, 1, 128, file->file)) == 0)
				return FALSE;
			if (bytes < 128) {
				file->buffer[2] = 0xfa; /* non-full record */
				memset(file->buffer + 3 + bytes, 0, 127 - bytes);
				file->buffer[0x82] = bytes;
			}
			else
				file->buffer[2] = 0xfc;	/* full record */
		}
		file->buffer[0x83] = SIO_ChkSum(file->buffer, 0x83);
	}
	file->block_length = length;
	return TRUE;
}

int IMG_TAPE_Read(IMG_TAPE_t *file, unsigned int *duration, int *is_gap, UBYTE *byte)
{
	if (file->was_writing) {
		CassetteFlush(file);
		file->was_writing = FALSE;
	}
	if (file->next_blockbyte >= file->block_length) {
		/* Buffer is exhausted, load next record. */
		int gap;

		if (!ReadNextRecord(file, &gap))
			return FALSE;
		file->next_blockbyte = 0;
		if (gap > 0) {
			/* Convert gap from ms to CPU ticks. */
			*duration = gap * 1789 + gap * 790 / 1000; /* (gap * 1789790 / 1000), avoiding overflow */
			*is_gap = TRUE;
			return TRUE;
		}
	}

	if (file->block_is_fsk) {
		/* Compose a 16-bit word with length of a signal in 1/10 of ms. */
		unsigned int len = file->buffer[file->next_blockbyte++];
		len |= ((unsigned int)file->buffer[file->next_blockbyte++]) << 8;

		/* Convert len from 1/10ms to CPU ticks. */
		*duration = len * 178 + len * 9790 / 10000; /* (len * 1789790 / 10000), avoiding overflow */
		*is_gap = TRUE;
	} else if (file->block_is_wav) {
		/* Compose a 32-bit little-endian CPU-tick count (see WAV_OpenAsCAS() -
		   unlike "fsk ", this needs no unit conversion: it's already exact). */
		unsigned int ticks = file->buffer[file->next_blockbyte++];
		ticks |= ((unsigned int)file->buffer[file->next_blockbyte++]) << 8;
		ticks |= ((unsigned int)file->buffer[file->next_blockbyte++]) << 16;
		ticks |= ((unsigned int)file->buffer[file->next_blockbyte++]) << 24;

		*duration = ticks;
		*is_gap = TRUE;
	} else {
		*byte = file->buffer[file->next_blockbyte++];
		*is_gap = FALSE;
		/* Next event will be after 10 bits of data gets loaded. */
		*duration = 10 * 1789790 / (file->isCAS ? file->block_baudrates[file->current_block] : 600);
	}
	return TRUE;
}

void IMG_TAPE_WriteAdvance(IMG_TAPE_t *file, unsigned int num_ticks)
{
	if (!file->was_writing) {
		file->savetime = 0;
		file->save_gap = 0;
		file->next_blockbyte = 0;
		file->block_length = 0;
		file->was_writing = TRUE;
		/* Always append to end of file. */
		file->current_block = file->num_blocks;
	}
	file->savetime += num_ticks;
}

int IMG_TAPE_WriteByte(IMG_TAPE_t *file, UBYTE byte, unsigned int pokey_counter)
{
	/* put_delay is time between end of last byte write / motor start, and
	   start of writing of current BYTE (in ms). */
	/* Note: byte duration in seconds: pokey_counter / (1789790/2) * 10
	 * in milliseconds: pokey_counter * 10 * 1000 / 1789790/2 */
	int put_delay = file->savetime /1790 - 10 * pokey_counter / 895; /* better accuracy not needed */
	if (put_delay > 05) {

		/* write previous block */
		if (file->block_length > 0) {
			if (!WriteRecord(file))
				return FALSE; /* Write error */
		}
		/* set new gap-time */
		file->save_gap += put_delay;
	}
	/* put byte into buffer */
	EnlargeBuffer(file, file->block_length + 1);
	file->buffer[file->block_length++] = byte;
	/* set new last byte-put time */
	file->savetime = 0;

	return TRUE;
}

int IMG_TAPE_Flush(IMG_TAPE_t *file)
{
	if (file->was_writing)
		return CassetteFlush(file);
	return TRUE;
}

/* Returns position in blocks/samples, counted from 0. */
unsigned int IMG_TAPE_GetPosition(IMG_TAPE_t *file)
{
	return file->current_block;
}
/* Returns size in blocks/samples. */
unsigned int IMG_TAPE_GetSize(IMG_TAPE_t *file)
{
	return file->num_blocks;
}

void IMG_TAPE_Seek(IMG_TAPE_t *file, unsigned int position)
{
	if (file->was_writing) {
		CassetteFlush(file);
		file->was_writing = FALSE;
	}
	file->current_block = (int)position;
	if (file->current_block > file->num_blocks)
		file->current_block = file->num_blocks;
	file->savetime = 0;
	file->save_gap = 0;
	file->next_blockbyte = 0;
	file->block_length = 0;
}

int IMG_TAPE_SerinStatus(IMG_TAPE_t *file, int event_time_left)
{
	if (file->was_writing || file->next_blockbyte == 0)
		return 1;
	if (file->block_is_fsk) {
		/* Signal can be computed from current position in the block -
		   first 2 bytes are SPACE (0), each next 2 bytes alternate between
		   MARK (1) and SPACE (0).

		   The complement matters: IMG_TAPE_Read() POST-increments
		   next_blockbyte, so while pulse k is being played the index
		   already sits at 2*(k+1). "~(x/2) & 1" therefore yields k&1 -
		   0 (SPACE) for the first pulse, as the CAS format requires -
		   whereas dropping the "~" yields (k+1)&1 and starts every
		   chunk on MARK. Turbo loaders that poll SKSTAT bit 4 directly
		   (rather than going through POKEY's UART) wait on a sustained
		   SPACE tone to synchronize, and never see it if this is
		   inverted. */
		return ~(file->next_blockbyte / 2) & 1;
	} else if (file->block_is_wav) {
		/* Same parity rule as the fsk branch above, just over 4-byte
		   (32-bit tick count) pulses instead of 2-byte ones. */
		return ~(file->next_blockbyte / 4) & 1;
	} else {
		int bit = 0; /* 0: stop bit, 1: 7th bit, ..., 8: 0th bit, 9: start bit */

		/* exam rate; if time_to_irq < duration of one byte */
		if (event_time_left <
			10 * 1789790 / (file->isCAS ? file->block_baudrates[file->current_block] : 600) - 1) {
			bit = event_time_left / (1789790 / (file->isCAS ? file->block_baudrates[file->current_block] : 600));
		}
		else {
			bit = 0;
		}

		/* if stopbit or out of range, return mark tone */
		if ((bit <= 0) || (bit > 9))
			return 1;

		/* if start bit, return space tone */
		if (bit == 9)
			return 0;

		/* eval tone to return */
		return (file->buffer[file->next_blockbyte - 1] >> (8 - bit)) & 1;
	}
}

int IMG_TAPE_SkipToData(IMG_TAPE_t *file, int ms)
{
	if (file->was_writing) {
		CassetteFlush(file);
		file->was_writing = FALSE;
	}

	while (ms > 0) {
		if (file->next_blockbyte < file->block_length) {
			if (file->block_is_fsk || file->block_is_wav) {
				/* FSK/WAV blocks are not supported during reads with patched
				   SIO, and skipped as a whole. */
				file->next_blockbyte = file->block_length;
			} else {
				int bytes = ms * (file->isCAS ? file->block_baudrates[file->current_block] : 600) / 1000 / 10;
				if (bytes > file->block_length - file->next_blockbyte)
					bytes = file->block_length - file->next_blockbyte;
				file->next_blockbyte += bytes;
				ms -= bytes * 10 * 1000 / (file->isCAS ? file->block_baudrates[file->current_block] : 600);
			}
			continue;
		}
		else {
			int gap;
			if (!ReadNextRecord(file, &gap))
				return FALSE;
			file->next_blockbyte = 0;
			ms -= gap;
		}
	}
	return TRUE;
}

int IMG_TAPE_ReadToMemory(IMG_TAPE_t *file, UWORD dest_addr, int length)
{
	int read_length;
	if (file->was_writing) {
		CassetteFlush(file);
		file->was_writing = FALSE;
	}

	read_length = file->block_length - file->next_blockbyte;

	if (read_length == 0) {
		/* No bytes left in current block, need to read next block. */
		int gap;
		if (!ReadNextRecord(file, &gap))
			/* EOF or read error */
			return -1;
		file->next_blockbyte = 0;
	}
	if (file->block_is_fsk || file->block_is_wav)
		/* FSK/WAV blocks are not supported during reads with patched SIO,
		   and always cause read failure. */
		return FALSE;

	/* Copy record to memory, excluding the checksum byte if it exists. */
	MEMORY_CopyToMem(file->buffer + file->next_blockbyte, dest_addr, read_length >= length ? length : read_length);
	file->next_blockbyte += (read_length >= length + 1 ? length + 1 : read_length);
	return read_length >= length + 1 &&
	       file->buffer[length] == SIO_ChkSum(file->buffer, length);
}

int IMG_TAPE_WriteFromMemory(IMG_TAPE_t *file, UWORD src_addr, int length, int gap)
{
	if (!file->was_writing) {
		file->savetime = 0;
		file->save_gap = 0;
		file->next_blockbyte = 0;
		file->block_length = 0;
		file->was_writing = TRUE;
	}
	EnlargeBuffer(file, length + 1);
	/* Put record into buffer. */
	MEMORY_CopyFromMem(src_addr, file->buffer, length);
	/* Eval checksum over buffer data. */
	file->buffer[length] = SIO_ChkSum(file->buffer, length);
	file->save_gap = gap;
	file->block_length = length + 1;
	return WriteRecord(file);
}
