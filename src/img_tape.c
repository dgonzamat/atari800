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
   run of samples between two consecutive amplitude-threshold crossings),
   with its length in samples. Returns FALSE to abort the scan. */
typedef int (*WAV_HalfCycleFn)(void *ctx, long run_samples);

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
				if (!fn(ctx, run_samples))
					return FALSE;
				run_samples = 0;
				state = new_state;
			}
			run_samples++;
		}
		frames_left -= got;
	}
	if (run_samples > 0 && !fn(ctx, run_samples))
		return FALSE;
	return TRUE;
}

/* Longest half-cycle length (in samples) tracked by the frequency
   histogram below; longer runs (silence, leader gaps) are folded into the
   last bucket - harmless, since they land far from the two tone clusters
   the histogram is trying to separate. */
enum { WAV_HIST_SIZE = 256 };

typedef struct {
	unsigned long hist[WAV_HIST_SIZE];
} WAV_HistCtx;

static int WAV_HistCollect(void *ctx_, long run_samples)
{
	WAV_HistCtx *ctx = (WAV_HistCtx *) ctx_;
	long idx = run_samples < 0 ? 0 : run_samples;
	if (idx >= WAV_HIST_SIZE)
		idx = WAV_HIST_SIZE - 1;
	ctx->hist[idx]++;
	return TRUE;
}

/* A real cassette player demodulates its two-tone FSK carrier in hardware
   before POKEY ever sees it: what the OS's tape-reading routine expects is
   a clean signal that only changes level at bit boundaries, not one
   transition per carrier half-cycle. WAV_ScanHalfCycles() above gives us
   the raw carrier's half-cycle lengths (in samples) - this splits them
   into the two tone-frequency clusters (a classic 2-means/Otsu-style split
   on the length histogram: converge a boundary so everything shorter than
   it - the higher-frequency tone - is one cluster, everything at or above
   it is the other), so the caller can classify and merge runs into a
   demodulated squarewave. Returns the boundary length. */
static long WAV_HistThreshold(const unsigned long *hist)
{
	long threshold, i;
	double total = 0, sum = 0;

	for (i = 0; i < WAV_HIST_SIZE; i++) {
		total += hist[i];
		sum += (double) i * hist[i];
	}
	if (total == 0)
		return 1;
	threshold = (long) (sum / total + 0.5);
	for (i = 0; i < 16; i++) {
		double sumA = 0, countA = 0, sumB = 0, countB = 0;
		long j, new_threshold;
		for (j = 0; j < WAV_HIST_SIZE; j++) {
			if (j < threshold) {
				sumA += (double) j * hist[j];
				countA += hist[j];
			}
			else {
				sumB += (double) j * hist[j];
				countB += hist[j];
			}
		}
		if (countA == 0 || countB == 0)
			break;
		new_threshold = (long) ((sumA / countA + sumB / countB) / 2.0 + 0.5);
		if (new_threshold == threshold)
			break;
		threshold = new_threshold;
	}
	return threshold < 1 ? 1 : threshold;
}

typedef struct {
	WAV_ChunkWriter *writer;
	int sample_rate;
	long threshold;
	int tone; /* -1 = none merged yet, 0 = short (high-frequency) tone, 1 = long (low-frequency) tone */
	long merged_samples;
} WAV_EmitCtx;

/* Classifies each half-cycle against the calibrated threshold and merges
   consecutive same-tone runs, only calling WAV_EmitRun() - i.e. only
   emitting an output pulse - when the classified tone actually changes.
   This is the demodulation step: it turns many carrier half-cycles of the
   same tone into the single bit-aligned pulse a real demodulator would
   have produced. */
static int WAV_EmitClassified(void *ctx_, long run_samples)
{
	WAV_EmitCtx *ctx = (WAV_EmitCtx *) ctx_;
	int tone = run_samples < ctx->threshold ? 0 : 1;

	if (ctx->tone == -1) {
		ctx->tone = tone;
		ctx->merged_samples = run_samples;
		return TRUE;
	}
	if (tone != ctx->tone) {
		if (!WAV_EmitRun(ctx->writer, ctx->merged_samples, ctx->sample_rate))
			return FALSE;
		ctx->tone = tone;
		ctx->merged_samples = run_samples;
	}
	else
		ctx->merged_samples += run_samples;
	return TRUE;
}

/* Converts the PCM data at DATA_OFFSET/DATA_SIZE in F into a synthetic CAS
   byte stream (FUJI header + one or more "wavp" chunks), written to OUT.
   Three passes over the audio: one to find the peak amplitude (needed to
   size the comparator's hysteresis band), one to calibrate the tone
   frequencies actually present in this recording (see WAV_HistThreshold()),
   and one to demodulate the carrier into pulses using that calibration
   (see WAV_EmitClassified()). */
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
	WAV_ChunkWriter writer;
	CAS_Header header;
	WAV_HistCtx hist_ctx;
	WAV_EmitCtx emit_ctx;

	if (num_frames <= 0 || frame_size <= 0)
		return FALSE;
	block = (UBYTE *) Util_malloc(WAV_READ_FRAMES * frame_size);

	/* Pass 1: peak amplitude. */
	if (fseek(f, data_offset, SEEK_SET) != 0) {
		free(block);
		return FALSE;
	}
	frames_left = num_frames;
	while (frames_left > 0) {
		long want = frames_left > WAV_READ_FRAMES ? WAV_READ_FRAMES : frames_left;
		long got = (long) fread(block, frame_size, want, f);
		long i;
		if (got <= 0)
			break;
		for (i = 0; i < got; i++) {
			long v = WAV_DownmixFrame(block + i * frame_size, channels, bytes_per_sample);
			long a = v < 0 ? -v : v;
			if (a > peak)
				peak = a;
		}
		frames_left -= got;
	}

	/* Header: FUJI marker (empty description) + baud (unused by wavp, but
	   every CAS file conventionally has one). */
	memset(&header, 0, sizeof(header));
	if (fwrite("FUJI", 1, 4, out) != 4 || fwrite(&header.length_lo, 1, 4, out) != 4)
		goto fail;
	header.aux_lo = DEFAULT_BAUDRATE & 0xFF;
	header.aux_hi = DEFAULT_BAUDRATE >> 8;
	if (fwrite("baud", 1, 4, out) != 4 || fwrite(&header.length_lo, 1, 4, out) != 4)
		goto fail;

	hi_thresh = (int) (peak * WAV_HYSTERESIS_PERCENT / 100);
	lo_thresh = -hi_thresh;

	/* Pass 2: histogram the carrier's half-cycle lengths, then calibrate a
	   frequency threshold that separates this recording's two tones. */
	memset(&hist_ctx, 0, sizeof(hist_ctx));
	if (!WAV_ScanHalfCycles(f, channels, bytes_per_sample, frame_size, data_offset, num_frames,
	                         block, hi_thresh, lo_thresh, WAV_HistCollect, &hist_ctx))
		goto fail;

	/* Pass 3: demodulate - classify each half-cycle against the calibrated
	   threshold and emit one pulse per merged same-tone run. */
	WAV_ChunkWriterInit(&writer, out);
	emit_ctx.writer = &writer;
	emit_ctx.sample_rate = sample_rate;
	emit_ctx.threshold = WAV_HistThreshold(hist_ctx.hist);
	emit_ctx.tone = -1;
	emit_ctx.merged_samples = 0;
	if (!WAV_ScanHalfCycles(f, channels, bytes_per_sample, frame_size, data_offset, num_frames,
	                         block, hi_thresh, lo_thresh, WAV_EmitClassified, &emit_ctx))
		goto fail;
	if (emit_ctx.tone != -1 && !WAV_EmitRun(&writer, emit_ctx.merged_samples, sample_rate))
		goto fail;
	if (!WAV_ChunkWriterFlush(&writer))
		goto fail;

	free(block);
	return TRUE;
fail:
	free(block);
	return FALSE;
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

/* Parses the CAS chunk stream in IMG->file (already positioned right after
   the initial 6-byte FUJI header, whose already-read bytes are passed in
   HEADER) into IMG's block table. Common tail for both real .CAS files and
   the synthetic CAS stream WAV files are converted into (see
   WAV_OpenAsCAS()). Returns FALSE on read error. */
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
		/* CAS file */
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
		   MARK (1) and SPACE (0). The previous "~x & 1" here started the
		   chunk at MARK instead of SPACE - inverted relative to both this
		   comment and the "mark tone"=1 / "space tone"=0 convention the
		   non-fsk branch below uses (and to the CAS format's own spec:
		   an "fsk " chunk always begins with the SPACE signal). */
		return (file->next_blockbyte / 2) & 1;
	} else if (file->block_is_wav) {
		/* Same parity trick as the fsk branch above, just over 4-byte
		   (32-bit tick count) pulses instead of 2-byte ones. */
		return (file->next_blockbyte / 4) & 1;
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
