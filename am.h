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

extern int am_open(char *filename, float carrier_freq, size_t len);
extern int am_get_samples(float *am_buffer);
extern void am_close();
