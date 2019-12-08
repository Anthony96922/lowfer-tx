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

#include <sndfile.h>
#include <stdlib.h>
#include <strings.h>
#include <math.h>

#include "lwtx.h"
#include "am.h"

#define FIR_HALF_SIZE	64
#define FIR_SIZE	(2*FIR_HALF_SIZE-1)

// coefficients of the low-pass FIR filter
float low_pass_fir[FIR_HALF_SIZE];
float fir_buffer[FIR_SIZE];

int fir_index;

// Carrier wave constants
float carrier_wave[SAMPLE_RATE];
int carrier_phase;
int carrier_wave_max;

size_t length;
float upsample_factor;

float *audio_buffer;

int audio_index;
int audio_len;
float audio_pos;

int channels;

SNDFILE *inf;

float *alloc_empty_buffer(size_t length) {
    float *p = malloc(length * sizeof(float));
    if(p == NULL) return NULL;

    bzero(p, length * sizeof(float));

    return p;
}

int am_open(char *filename, float carrier_freq, size_t len) {
	length = len;

	if(filename != NULL) {
		// Open the input file
		SF_INFO sfinfo;

		// stdin or file on the filesystem?
		if(filename[0] == '-') {
			if(!(inf = sf_open_fd(fileno(stdin), SFM_READ, &sfinfo, 0))) {
				fprintf(stderr, "Error: could not open stdin for audio input.\n");
				return -1;
			} else {
				printf("Using stdin for audio input.\n");
			}
		} else {
			if(!(inf = sf_open(filename, SFM_READ, &sfinfo))) {
				fprintf(stderr, "Error: could not open input file %s.\n", filename);
				return -1;
			} else {
				printf("Using audio file: %s\n", filename);
			}
		}

		int in_samplerate = sfinfo.samplerate;
		upsample_factor = (float) SAMPLE_RATE / in_samplerate;
		channels = sfinfo.channels;

		printf("Input: %d Hz, %d channels, upsampling factor: %.2f\n", in_samplerate, channels, upsample_factor);

		int cutoff_freq = 4000;
		if(in_samplerate/2 < cutoff_freq) cutoff_freq = in_samplerate/2;

		// Here we divide this coefficient by two because it will be counted twice
		// when applying the filter
		low_pass_fir[FIR_HALF_SIZE-1] = 2 * cutoff_freq / SAMPLE_RATE /2;

		// Only store half of the filter since it is symmetric
		for(int i=1; i<FIR_HALF_SIZE; i++) {
			low_pass_fir[FIR_HALF_SIZE-1-i] =
				sin(2 * M_PI * cutoff_freq * i / SAMPLE_RATE) / (M_PI * i) // sinc
				* (.54 - .46 * cos(2 * M_PI * (i+FIR_HALF_SIZE) / (2*FIR_HALF_SIZE))); // Hamming window
		}

		//printf("Created low-pass FIR filter for audio channels, with cutoff at %d Hz\n", cutoff_freq);

		audio_pos = upsample_factor;
		audio_buffer = alloc_empty_buffer(length * channels);
		if(audio_buffer == NULL) return -1;
	}

	int sine_half_cycles = 0;

	for (int i = 0; i < SAMPLE_RATE; i++) {
		carrier_wave[carrier_wave_max] = sin(2 * M_PI * carrier_freq * carrier_wave_max / SAMPLE_RATE);
		if (carrier_wave[carrier_wave_max] < 0.000000001 && carrier_wave[carrier_wave_max] > -0.000000001) {
			sine_half_cycles++;
			if (sine_half_cycles == 3) break;
		}
		carrier_wave_max++;
	}

	//printf("Created %d carrier wave constants for %.1f kHz\n", carrier_wave_max, carrier_freq/1000);

	return 0;
}

int am_get_samples(float *am_buffer) {

	if (inf == NULL) {
		for (int j = 0; j < length; j++) {
			am_buffer[j] = 0.9 * carrier_wave[carrier_phase++];
			if (carrier_phase == carrier_wave_max) carrier_phase = 0;
		}
		return 0;
	}

	for(int i=0; i<length; i++) {
		if(audio_pos >= upsample_factor) {
			audio_pos -= upsample_factor;

			if(audio_len <= channels) {
				for(int j=0; j<2; j++) { // one retry
					audio_len = sf_read_float(inf, audio_buffer, length);
					if (audio_len < 0) {
						fprintf(stderr, "Error reading audio\n");
						return -1;
					} else if (audio_len == 0) {
						if( sf_seek(inf, 0, SEEK_SET) < 0 ) break;
					} else {
						break;
					}
				}
				audio_index = 0;
			} else {
				audio_index += channels;
				audio_len -= channels;
			}
		}
		audio_pos++;

		// First store the current sample(s) into the FIR filter's ring buffer
		if(channels == 2) {
			// downmix stereo to mono
			fir_buffer[fir_index] = (audio_buffer[audio_index] + audio_buffer[audio_index+1]) / 2;
		} else {
			fir_buffer[fir_index] = audio_buffer[audio_index];
		}
		fir_index++;
		if(fir_index == FIR_SIZE) fir_index = 0;

		// Now apply the FIR low-pass filter

		/* As the FIR filter is symmetric, we do not multiply all
		   the coefficients independently, but two-by-two, thus reducing
		   the total number of multiplications by a factor of two
		 */
		float out = 0;
		int ifbi = fir_index;  // ifbi = increasing FIR Buffer Index
		int dfbi = fir_index;  // dfbi = decreasing FIR Buffer Index
		for(int fi=0; fi<FIR_HALF_SIZE; fi++) {  // fi = Filter Index
			dfbi--;
			if(dfbi < 0) dfbi = FIR_SIZE-1;
			out += low_pass_fir[fi] * (fir_buffer[ifbi] + fir_buffer[dfbi]);
			ifbi++;
			if(ifbi == FIR_SIZE) ifbi = 0;
		}
		// End of FIR filter

		// Amplitude modulation (A3E)
		am_buffer[i] =
			carrier_wave[carrier_phase] * 0.5 +
			carrier_wave[carrier_phase] * out * 0.5;

		carrier_phase++;
		if (carrier_phase == carrier_wave_max) carrier_phase = 0;
	}

	return 0;
}

void am_close() {
	if(sf_close(inf)) fprintf(stderr, "Error closing audio file\n");

	if(audio_buffer != NULL) free(audio_buffer);
}
