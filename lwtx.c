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
#include <string.h>
#include <math.h>
#include "lwtx.h"
#include "am.h"

static uint8_t stop_tx;

static void stop() {
	stop_tx = 1;
}

static int8_t tx(char *audio, float freq_k, float vol) {
	/* RF output data */
	char *rf_data;
	/* AO */
	ao_device *device;
	ao_sample_format format;
	/* VFO */
	struct wave_t tx_vfo;

	int32_t samples;

	/* Gracefully stop the transmitter on SIGINT or SIGTERM */
	signal(SIGINT, stop);
	signal(SIGTERM, stop);

	rf_data = malloc(DATA_SIZE * 4 * sizeof(char));

	memset(&format, 0, sizeof(format));
	format.bits = 16;
	format.channels = 2;
	format.rate = SAMPLE_RATE;
	format.byte_format = AO_FMT_LITTLE;

	ao_initialize();

	device = ao_open_live(ao_default_driver_id(), &format, NULL);
	if (device == NULL) {
		fprintf(stderr, "Error: cannot open sound device.\n");
		goto exit;
	}

	/* Initialize VFO */
	init_vfo(&tx_vfo, SAMPLE_RATE);

	/* Set frequency */
	printf("Setting VFO to %.1f kHz.\n", freq_k);
	set_vfo(&tx_vfo, freq_k * 1000.0f);

	/* Set TX power */
	printf("Setting transmit power to %.1f%%.\n", vol);
	set_vfo_power(&tx_vfo, vol);

	if (init_input(&tx_vfo, audio) < 0) goto exit;

	printf("Beginning to transmit.\n");

	while (1) {
		if ((samples = rf_get_samples(&tx_vfo, rf_data)) < 0) break;

		/* TX */
		if (!ao_play(device, rf_data, samples * 2 * sizeof(int16_t))) {
			fprintf(stderr, "Error: could not play audio.\n");
			break;
		}

		if (stop_tx) {
			printf("Stopping...\n");
			break;
		}
	}

	/* Clean up */
	exit_vfo(&tx_vfo);
	exit_input();
exit:
	ao_close(device);
	ao_shutdown();
	free(rf_data);

	return 0;
}

int main(int argc, char **argv) {
	int opt;
	char *audio = NULL;
	float freq = 174.0f;
	float txpwr = 5.0f;
	float max_freq = (SAMPLE_RATE / 1000 / 2.0f) * 0.96f;
	uint8_t nyquist_check = 1;

	const char	*short_opt = "a:f:p:nh";
	struct option	long_opt[] =
	{
		{"audio",	required_argument,	NULL,	'a'},
		{"freq",	required_argument,	NULL,	'f'},
		{"power",	required_argument,	NULL,	'p'},
		{"nyq",		no_argument,		NULL,	'n'},

		{"help",	no_argument,		NULL,	'h'},
		{0,		0,			0,	0}
	};

	while ((opt = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1) {
		switch(opt) {
			case 'a':
				audio = optarg;
				break;

			case 'f':
				freq = atof(optarg);
				break;

			case 'p':
				txpwr = atof(optarg);
				break;

			case 'n':
				nyquist_check = 0;
				break;

			case 'h':
			case '?':
			default:
				fprintf(stderr,
				"LWTX: longwave transmitter for amateur or LowFER\n"
				"\n"
				"Usage: %s\n"
				"    [-a,--audio audio file]\n"
				"    [-f,--freq frequency (kHz)]\n"
				"    [-p,--power tx-power (%%)]\n"
				"\n"
				" NOTE! Depending on the sound card used, a filter may be needed\n"
				" to limit out of band signals. Do not attach an amplifier\n"
				" unless you know the output is clean or have a filter to keep\n"
				" harmonics to a safe level. You've been warned.\n"
				"\n", argv[0]);
				return 1;
				break;
		}
	}

	if (freq < 170.0f || freq > 180.0f) {
		fprintf(stderr, "Frequency should be between 170-180 kHz for LowFER operation.\n");
	}

	if (!nyquist_check && freq > max_freq) {
		fprintf(stderr, "Frequency must be below %.1f kHz.\n", max_freq);
		return -1;
	}

	if (txpwr < 0.0f || txpwr > 100.0f) {
		fprintf(stderr, "Transmit power must be between 0-100.\n");
		return 1;
	}

	return tx(audio, freq, txpwr);
}
