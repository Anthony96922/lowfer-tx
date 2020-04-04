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

int channels;

SNDFILE *inf;

// SRC
int src_errorcode;

SRC_STATE *src_state;
SRC_DATA src_data;

// Carrier wave constants
float carrier_wave[SAMPLE_RATE];
int carrier_phase;
int carrier_wave_max;

char *audio_file = NULL;

int rf_init(char *audio, float carrier_freq) {

	// Set VFO
	int sine_half_cycles = 0;
	float tmp;

	for (int i = 0; i < SAMPLE_RATE; i++) {
		tmp = sin(2 * M_PI * carrier_freq * carrier_wave_max / SAMPLE_RATE);
			if (i && tmp < 0.1e-6 && tmp > -0.1e-6) {
			if (sine_half_cycles++ == 3) break;
		}
		carrier_wave[carrier_wave_max++] = tmp;
	}

	if (audio == NULL) return 0;

	audio_file = audio;

	SF_INFO sfinfo;

	if (audio_file[0] == '-') {
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

	int in_samplerate = sfinfo.samplerate;
	float upsample_factor = SAMPLE_RATE / in_samplerate;
	int channels = sfinfo.channels;

	printf("Input: %d Hz, %d channels, upsampling factor: %.2f\n", in_samplerate, channels, upsample_factor);

	audio_input = malloc(INPUT_DATA_SIZE * channels * sizeof(float));
	resampled_input = malloc(DATA_SIZE * channels * sizeof(float));

	src_data.src_ratio = upsample_factor;
	src_data.output_frames = DATA_SIZE;
	src_data.data_in = audio_input;
	src_data.data_out = resampled_input;

	if ((src_state = src_new(SRC_SINC_FASTEST, channels, &src_errorcode)) == NULL) {
		fprintf(stderr, "Error: src_new failed: %s\n", src_strerror(src_errorcode));
		return -1;
	}

	return 0;
}

int rf_get_samples(float *rf_buffer) {
	int buf_size = 0;
	int j = 0;

	if (audio_file == NULL) {
		buf_size = INPUT_DATA_SIZE;
		for (int i = 0; i < buf_size; i++) {
			rf_buffer[i] = carrier_wave[carrier_phase] * 0.5;
			if (++carrier_phase == carrier_wave_max) carrier_phase = 0;
		}
		return buf_size;
	}

	int audio_len;

read_audio:
	audio_len = sf_readf_float(inf, audio_input, INPUT_DATA_SIZE);

	if (audio_len < 0) {
		fprintf(stderr, "Error reading audio\n");
		return -1;
	} else if (audio_len == 0) {
		if (sf_seek(inf, 0, SEEK_SET) < 0) {
			memset(resampled_input, 0, INPUT_DATA_SIZE * sizeof(float));
			buf_size = INPUT_DATA_SIZE;
		} else goto read_audio; // Try to get new audio
	} else {
		src_data.input_frames = audio_len;
		if ((src_errorcode = src_process(src_state, &src_data))) {
			fprintf(stderr, "Error: src_process failed: %s\n", src_strerror(src_errorcode));
			return -1;
		}
		buf_size = src_data.output_frames_gen;
	}

	for (int i = 0; i < buf_size; i++) {
		float out;
		if (channels == 2) {
			out = (resampled_input[j] + resampled_input[j+1]) / 2;
			j += 2;
		} else {
			out = resampled_input[j];
			j++;
		}

		// Amplitude Modulation (A3E)
		rf_buffer[i] =
			carrier_wave[carrier_phase] * 0.5 +
			carrier_wave[carrier_phase] * 0.5 * out;

		if (++carrier_phase == carrier_wave_max) carrier_phase = 0;
	}

	return buf_size;
}

void rf_exit() {
	if (sf_close(inf)) fprintf(stderr, "Error closing audio file\n");

	if (audio_input != NULL) free(audio_input);
	if (resampled_input != NULL) free(resampled_input);
	src_delete(src_state);
}
