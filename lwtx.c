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
#include <signal.h>
#include <getopt.h>
#include <ao/ao.h>

#include "lwtx.h"
#include "am.h"

int stop_tx;

void stop() {
	stop_tx = 1;
}

float volume;

void postprocess(float *inbuf, short *outbuf, size_t inbufsize) {
	int j = 0;

	for (int i = 0; i < inbufsize; i++) {
		outbuf[j] = outbuf[j+1] = (inbuf[i] * (volume/100)) * 32767;
		j += 2;
	}
}

int tx(char *audio, float freq_k, float vol) {
	// Gracefully stop the transmitter on SIGINT or SIGTERM
	signal(SIGINT, stop);
	signal(SIGTERM, stop);

	// RF output data
	float rf_data[DATA_SIZE];
	short dev_out[DATA_SIZE];

	// AO
	ao_device *device;
	ao_sample_format format;
	format.bits = 16;
	format.channels = 2;
	format.rate = SAMPLE_RATE;
	format.byte_format = AO_FMT_LITTLE;

	ao_initialize();

	if ((device = ao_open_live(ao_default_driver_id(), &format, NULL)) == NULL) {
		fprintf(stderr, "Error: cannot open sound device.\n");
		return 1;
	}

	// Initialize
	if (rf_init(audio, freq_k * 1000) < 0) {
		rf_exit();
		return 1;
	}

	volume = vol;

	printf("Starting to transmit on %.1f kHz.\n", freq_k);

	int samples;

	for (;;) {
		if ((samples = rf_get_samples(rf_data)) < 0) break;

		postprocess(rf_data, dev_out, samples);

		// TX
		if (!ao_play(device, (char *)dev_out, samples * 2 * 2)) {
			fprintf(stderr, "Error: could not play audio.\n");
			break;
		}

		if (stop_tx) {
			printf("Stopping...\n");
			break;
		}
	}

	// Clean up
	rf_exit();

	ao_close(device);
	ao_shutdown();

	return 0;
}

int main(int argc, char **argv) {
	int opt;
	char *audio = NULL;
	float freq = 174;
	float txpwr = 10;

	const char	*short_opt = "a:f:p:h";
	struct option	long_opt[] =
	{
		{"audio",	required_argument, NULL, 'a'},
		{"freq",	required_argument, NULL, 'f'},
		{"power",	required_argument, NULL, 'p'},

		{"help",	no_argument, NULL, 'h'},
		{ 0,		0,		0,	0 }
	};

	while((opt = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1)
	{
		switch(opt)
		{
			case 'a':
				audio = optarg;
				break;

			case 'f':
				freq = atof(optarg);
				break;

			case 'p':
				txpwr = atoi(optarg);
				break;

			case 'h':
				fprintf(stderr, "Usage: %s [--audio (-a) audio file] [--freq (-f) freq (kHz)] [--power (-p) tx-power]\n", argv[0]);
				return 1;
				break;

			default:
				fprintf(stderr, "(See -h / --help)\n");
				return 1;
				break;
		}
	}

	if (freq < 170 || freq > 180) {
		fprintf(stderr, "Frequency should be between 170 - 180 kHz for LowFER operation.\n");
	}

	if (freq > (SAMPLE_RATE/1000)/2) {
		fprintf(stderr, "Frequency must be below %d kHz.\n", (SAMPLE_RATE/1000)/2);
		return -1;
	}

	if (txpwr < 1 || txpwr > 100) {
		fprintf(stderr, "Transmit power must be between 1 - 100.\n");
		return 1;
	}

	return tx(audio, freq, txpwr);
}
