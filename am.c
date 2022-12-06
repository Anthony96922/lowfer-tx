/*
 * lowfer-tx - LowFER transmitter using a sound card
 * Copyright (C) 2019 Anthony96922
 *
 * See https://github.com/Anthony96922/lowfer-tx
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sndfile.h>
#include <samplerate.h>

#include "lwtx.h"
#include "am.h"

static float *audio_input;
static float *resampled_input;
static uint8_t audio_input_used;

static SNDFILE *inf;

static SRC_STATE *src_state;
static SRC_DATA src_data;

void init_vfo(wave_t *vfo, uint32_t sample_rate) {
	memset(vfo, 0, sizeof(wave_t));
	vfo->srate = sample_rate;
	vfo->wave_i = malloc(sample_rate * sizeof(float));
	vfo->wave_q = malloc(sample_rate * sizeof(float));
}

void set_vfo(wave_t *vfo, uint32_t frequency) {
	uint8_t sine_half_cycles = 0;
	uint32_t max = 0;
	double sample_i, sample_q;

	vfo->freq = frequency;
	memset(vfo->wave_i, 0, vfo->srate * sizeof(float));
	memset(vfo->wave_q, 0, vfo->srate * sizeof(float));

	for (uint32_t i = 0; i < vfo->srate; i++) {
		sample_i = cos(2.0 * M_PI * vfo->freq * max / vfo->srate);
		sample_q = sin(2.0 * M_PI * vfo->freq * max / vfo->srate);
		if (sample_q < 0.1e-6 && sample_q > -0.1e-6) {
			if (sine_half_cycles++ == 2) break;
		} else {
			vfo->wave_i[max] = (float)sample_i;
			vfo->wave_q[max] = (float)sample_q;
		}
		max++;
	}

	vfo->max = max;
}

int8_t init_input(wave_t *vfo, char *audio) {
	SF_INFO sfinfo;

	if (audio == NULL) return 0;

	if (strcmp(audio, "-") == 0) {
		if ((inf = sf_open_fd(fileno(stdin), SFM_READ, &sfinfo, 0))) {
			printf("Using stdin for audio input.\n");
		} else {
			fprintf(stderr, "Error: could not open stdin for audio input.\n");
			return -1;
		}
	} else {
		if ((inf = sf_open(audio, SFM_READ, &sfinfo))) {
			printf("Using audio file: %s\n", audio);
		} else {
			fprintf(stderr, "Error: could not open input file %s.\n", audio);
			return -1;
		}
	}

	if (sfinfo.channels == 2) {
		printf("Input is 2 channel. Using IQ modulator\n");
	} else if (sfinfo.channels == 1) {
		printf("Input is 1 channel. Using AM\n");
	} else {
		fprintf(stderr, "Invalid number of channels\n");
		return -1;
	}

	vfo->channels = sfinfo.channels;

	/* output sample rate */
	printf("Input sample rate: %d\n", sfinfo.samplerate);

	audio_input = malloc(INPUT_DATA_SIZE * sizeof(float) * sfinfo.channels);
	resampled_input = malloc(DATA_SIZE * sizeof(float) * sfinfo.channels);

	src_data.src_ratio = (double)vfo->srate / (double)sfinfo.samplerate;
	src_data.input_frames = INPUT_DATA_SIZE;
	src_data.output_frames = DATA_SIZE;
	src_data.data_in = audio_input;
	src_data.data_out = resampled_input;

	int src_error;

	if ((src_state = src_new(SRC_SINC_FASTEST, sfinfo.channels, &src_error)) == NULL) {
		fprintf(stderr, "Error: src_new failed: %s\n",
			src_strerror(src_error));
		return -1;
	}

	audio_input_used = 1;

	return 0;
}

void set_vfo_power(wave_t *vfo, float p) {
	if (p >= 0.0f && p <= 100.0f) vfo->txpwr = p / 100.0f;
}

static inline int32_t get_audio(wave_t *vfo) {
	int audio_len = 0;
	int frames_to_read = INPUT_DATA_SIZE;
	int read_len;

	while (frames_to_read > 0 && audio_len < INPUT_DATA_SIZE) {
		read_len = sf_readf_float(inf,
			audio_input + audio_len * vfo->channels,
			frames_to_read);
		if (audio_len < 0) {
			fprintf(stderr, "Error reading audio\n");
			return -1;
		}

		audio_len += read_len;
		frames_to_read -= audio_len;
		if (audio_len == 0 && sf_seek(inf, 0, SEEK_SET) < 0) return -1;
	}

	if ((src_process(src_state, &src_data))) return -1;

	return src_data.output_frames_gen;
}

static inline int32_t rf_get_carrier(wave_t *vfo, float *buf) {
	for (uint32_t i = 0; i < INPUT_DATA_SIZE; i++) {
		/* CW */
		buf[i] = vfo->wave_i[vfo->phase];

		/* TX power adjustment */
		buf[i] *= vfo->txpwr;

		if (++vfo->phase == vfo->max) vfo->phase = 0;
	}
	return INPUT_DATA_SIZE;
}

static inline int32_t rf_get_am(wave_t *vfo, float *buf) {
	int32_t audio_len;

	audio_len = get_audio(vfo);
	if (audio_len < 0) return -1;

	for (int32_t i = 0; i < audio_len; i++) {
		/* CW */
		buf[i] = vfo->wave_i[vfo->phase] * 0.5f;

		/* Amplitude Modulation (A3E) */
		buf[i] *= (resampled_input[i] + 1.0f) * 0.5f;

		/* TX power adjustment */
		buf[i] *= vfo->txpwr;

		if (++vfo->phase == vfo->max) vfo->phase = 0;
	}

	return audio_len;
}

static inline int32_t rf_get_iq(wave_t *vfo, float *buf) {
	int32_t audio_len;
	int32_t j = 0;

	audio_len = get_audio(vfo);
	if (audio_len < 0) return -1;

	for (int32_t i = 0; i < audio_len; i++) {
		buf[i] = 0.0f;

		/* I */
		buf[i] += vfo->wave_i[vfo->phase] * resampled_input[j+0];

		/* Q */
		buf[i] += vfo->wave_q[vfo->phase] * resampled_input[j+1];

		/* TX power adjustment */
		buf[i] *= vfo->txpwr;

		j += 2;

		if (++vfo->phase == vfo->max) vfo->phase = 0;
	}

	return audio_len;
}

int32_t rf_get_samples(wave_t *vfo, float *rf_buffer) {
	int32_t ret;
	if (audio_input_used) {
		if (vfo->channels == 2) {
			ret = rf_get_iq(vfo, rf_buffer);
		} else {
			ret = rf_get_am(vfo, rf_buffer);
		}
	} else {
		ret = rf_get_carrier(vfo, rf_buffer);
	}
	return ret;
}

void exit_input() {
	if (audio_input_used) {
		if (sf_close(inf))
			fprintf(stderr, "Error closing audio file\n");
		free(audio_input);
		free(resampled_input);
		src_delete(src_state);
	}
}

void exit_vfo(wave_t *vfo) {
	free(vfo->wave_i);
	free(vfo->wave_q);
}
