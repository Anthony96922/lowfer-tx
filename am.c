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

float *audio_input;
float *resampled_input;

SNDFILE *inf;

SRC_STATE *src_state;
SRC_DATA src_data;

// Carrier wave constants
float carrier_wave[SAMPLE_RATE];
int carrier_phase;
int carrier_wave_max;

char *audio_file = NULL;

void set_vfo(float carrier_freq) {
	int sine_half_cycles = 0;

	for (int i = 0; i < SAMPLE_RATE; i++) {
		float tmp = sin(2 * M_PI * carrier_freq * carrier_wave_max / SAMPLE_RATE);
		if (tmp < 0.1e-6 && tmp > -0.1e-6 && sine_half_cycles++ == 2) break;
		carrier_wave[carrier_wave_max++] = tmp;
	}
}

int rf_init(char *audio) {
	if (audio == NULL) return 0;

	audio_file = audio;

	SF_INFO sfinfo;

	if (strcmp(audio_file, "-") == 0) {
		// For wwvsim
		sfinfo.format = SF_FORMAT_RAW | SF_FORMAT_PCM_16;
		sfinfo.samplerate = 16000;
		sfinfo.channels = 1;

		if (!(inf = sf_open_fd(fileno(stdin), SFM_READ, &sfinfo, 0))) {
			fprintf(stderr, "Error: could not open stdin for audio input.\n");
			return -1;
		} else {
			printf("Using stdin for audio input.\n");
		}
	} else {
		if (!(inf = sf_open(audio_file, SFM_READ, &sfinfo))) {
			fprintf(stderr, "Error: could not open input file %s.\n", audio);
			return -1;
		} else {
			printf("Using audio file: %s\n", audio);
		}
	}

	if (sfinfo.channels > 1) {
		fprintf(stderr, "Input must be 1 channel\n");
		return -1;
	}

	int in_samplerate = sfinfo.samplerate;
	float upsample_factor = SAMPLE_RATE / in_samplerate;

	printf("Input: %d Hz, upsampling factor: %.2f\n", in_samplerate, upsample_factor);

	audio_input = malloc(INPUT_DATA_SIZE * sizeof(float));
	resampled_input = malloc(DATA_SIZE * sizeof(float));

	src_data.src_ratio = upsample_factor;
	src_data.output_frames = DATA_SIZE;
	src_data.data_in = audio_input;
	src_data.data_out = resampled_input;

	int src_error;

	if ((src_state = src_new(SRC_SINC_FASTEST, 1, &src_error)) == NULL) {
		fprintf(stderr, "Error: src_new failed: %s\n", src_strerror(src_error));
		return -1;
	}

	return 0;
}

float txpower;

void set_power(float vol) {
	if (vol <= 100) txpower = vol / 100;
}

int get_audio() {
	int audio_len;

read_audio:
	audio_len = sf_read_float(inf, audio_input, INPUT_DATA_SIZE);

	if (audio_len < 0) {
		fprintf(stderr, "Error reading audio\n");
		return -1;
	} else if (audio_len == 0) {
		if (sf_seek(inf, 0, SEEK_SET) < 0) {
			memset(resampled_input, 0, INPUT_DATA_SIZE * sizeof(float));
			audio_len = INPUT_DATA_SIZE;
		} else goto read_audio; // Try to get new audio
	} else {
		src_data.input_frames = audio_len;
		int src_error;
		if ((src_error = src_process(src_state, &src_data))) {
			fprintf(stderr, "Error: src_process failed: %s\n", src_strerror(src_error));
			return -1;
		}
		audio_len = src_data.output_frames_gen;
	}

	return audio_len;
}

int rf_get_samples(float *rf_buffer) {
	int audio_len;

	if (audio_file == NULL) {
		audio_len = INPUT_DATA_SIZE;
		for (int i = 0; i < audio_len; i++) {
			rf_buffer[i] = carrier_wave[carrier_phase] * 0.5;
			if (++carrier_phase == carrier_wave_max) carrier_phase = 0;
		}
		return audio_len;
	}

	if ((audio_len = get_audio()) < 0) return -1;

	for (int i = 0; i < audio_len; i++) {
		// Amplitude Modulation (A3E)
		rf_buffer[i] = carrier_wave[carrier_phase] * 0.5 * (resampled_input[i] + 1);

		if (++carrier_phase == carrier_wave_max) carrier_phase = 0;

		rf_buffer[i] *= txpower;
	}

	return audio_len;
}

void rf_exit() {
	if (sf_close(inf)) fprintf(stderr, "Error closing audio file\n");

	if (audio_input != NULL) free(audio_input);
	if (resampled_input != NULL) free(resampled_input);
	src_delete(src_state);
}
