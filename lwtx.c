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

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <getopt.h>
#include <ao/ao.h>

#include "lwtx.h"
#include "am.h"

#define DATA_SIZE 4096
#define OUTPUT_DATA_SIZE (DATA_SIZE * 2)

int stop_tx;

void stop() {
	stop_tx = 1;
}

int out_channels = 2;
float volume;

void postprocess(float *inbuf, short *outbuf, size_t inbufsize) {
	int j = 0;

	for (int i = 0; i < inbufsize; i++) {
		if (inbuf[i] <= -1 || inbuf[i] >= 1) {
			fprintf(stderr, "overshoot! (%.7f)\n", inbuf[i]);
		}
		// volume control
		inbuf[i] *= (volume / 100);
		// scale samples
		inbuf[i] *= 0x7fff;

		if (out_channels == 2) {
			// stereo upmix
			outbuf[j] = outbuf[j+1] = inbuf[i];
			j += 2;
		} else {
			outbuf[i] = inbuf[i];
		}
	}
}

int tx(char *audio_file, float freq, float vol) {
	// Gracefully stop the transmitter on SIGINT or SIGTERM
	int signals[] = {SIGINT, SIGTERM};
	for (int i = 0; i < 2; i++) {
		struct sigaction sa;

		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = stop;
		sigaction(signals[i], &sa, NULL);
	}

	// RF output data
	float rf_data[DATA_SIZE];
	short dev_out[OUTPUT_DATA_SIZE];

	// AO
	ao_device *device;
	ao_sample_format format;
	format.bits = 16;
	format.channels = 2;
	format.rate = SAMPLE_RATE;
	format.byte_format = AO_FMT_LITTLE;

	ao_initialize();
	int ao_driver = ao_default_driver_id();

	if ((device = ao_open_live(ao_driver, &format, NULL)) == NULL) {
		fprintf(stderr, "Error: cannot open sound device.\n");
		return 1;
	}

	// Initialize the RF generator
	if(am_open(audio_file, freq * 1000, DATA_SIZE) < 0) return 1;

	volume = vol;

	printf("Starting to transmit on %.1f kHz.\n", freq);

	// bytes = generated_frames * channels * bytes per sample
	int bytes = DATA_SIZE * out_channels * 2;

	for (;;) {
		if (am_get_samples(rf_data) < 0) break;

		postprocess(rf_data, dev_out, DATA_SIZE);
		// num_bytes = generated_frames * channels * bytes per sample
		if (!ao_play(device, (char *)dev_out, bytes)) {
			fprintf(stderr, "Error: could not play audio.\n");
			break;
		}

		if (stop_tx) {
			printf("Stopping...\n");
			break;
		}
	}

	am_close();

	ao_close(device);
	ao_shutdown();

	return 0;
}

int main(int argc, char **argv) {
	int opt;
	char *audio_file = NULL;
	float freq = 174;
	float txpwr = 100;

	const char	*short_opt = "a:f:p:h";
	struct option	long_opt[] =
	{
		{"audio", 	required_argument, NULL, 'a'},
		{"freq",	required_argument, NULL, 'f'},
		{"power",	required_argument, NULL, 'p'},

		{"help",	no_argument, NULL, 'h'},
		{ 0,		0,		0,	0 }
	};

	while((opt = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1)
	{
		switch(opt)
		{
			case 'a': //audio
				audio_file = optarg;
				break;

			case 'f': //freq
				freq = atof(optarg);
				break;

			case 'p': //tx power
				txpwr = atoi(optarg);
				break;

			case 'h': //help
				fprintf(stderr, "Usage: %s [--audio (-a) file] [--freq (-f) freq (kHz)] [--power (-p) tx-power]\n", argv[0]);
				return 1;
				break;

			default:
				fprintf(stderr, "(See -h / --help)\n");
				return 1;
				break;
		}
	}

	if (audio_file == NULL) {
		fprintf(stderr, "No audio file specified. Transmitting unmodulated carrier.\n");
	}

	if (freq < 170 || freq > 180) {
		fprintf(stderr, "Frequency should be between 170 - 180 kHz for LowFER operation.\n");
	}

	if (txpwr < 1 || txpwr > 100) {
		fprintf(stderr, "Transmit power must be between 1 - 100.\n");
		return 1;
	}

	int errcode = tx(audio_file, freq, txpwr);

	return errcode;
}
