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

#include <stdint.h>

/* Carrier wave */
typedef struct wave_t {
	uint32_t freq;
	uint32_t srate;
	float *wave_i;
	float *wave_q;
	uint32_t phase;
	uint32_t max;
	float txpwr;
	uint8_t channels;
} wave_t;

extern int8_t init_input(struct wave_t *vfo, char *audio);
extern void init_vfo(struct wave_t *vfo, uint32_t sample_rate);
extern void set_vfo(struct wave_t *vfo, uint32_t frequency);
extern void set_vfo_power(struct wave_t *vfo, float vol);
extern int32_t rf_get_samples(struct wave_t *vfo, char *rf_buffer);
extern void exit_vfo(struct wave_t *vfo);
extern void exit_input();
