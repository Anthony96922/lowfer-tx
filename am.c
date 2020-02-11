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
#include <strings.h>
#include <math.h>

#include "lwtx.h"
#include "am.h"

// Carrier wave constants
float carrier_wave[SAMPLE_RATE];
int carrier_phase;
int carrier_wave_max;

size_t length;

float get_tick() {
	static int phase;
	float t = 0;

	if (phase <= SAMPLE_RATE/100)
		t = sin(2 * M_PI * 1000 * phase/SAMPLE_RATE) / 2;

	if (++phase == SAMPLE_RATE) phase = 0;

	return t;
}

void am_create_carrier(float carrier_freq, size_t len) {
	length = len;

	int sine_half_cycles = 0;
	float tmp;

	for (int i = 0; i < SAMPLE_RATE; i++) {
		tmp = sin(2 * M_PI * carrier_freq * carrier_wave_max / SAMPLE_RATE);
		if (i && tmp < 0.1e-6 && tmp > -0.1e-6) {
			if (sine_half_cycles++ == 3) break;
		}
		carrier_wave[carrier_wave_max++] = tmp;
	}
}

void am_get_samples(float *am_buffer) {
	for (int i = 0; i < length; i++) {

		// Amplitude Modulation (A2A)
		am_buffer[i] =
			carrier_wave[carrier_phase] * 0.5 +
			carrier_wave[carrier_phase] * get_tick() * 0.5;

		// Carrier Wave (N0N)
		//am_buffer[i] = carrier_wave[carrier_phase];

		if (++carrier_phase == carrier_wave_max) carrier_phase = 0;
	}
}
