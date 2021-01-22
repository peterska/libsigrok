/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Peter Skarpetis <peters@skarpetis.com>
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

#ifndef LIBSIGROK_HARDWARE_MHINSTEK_MHS_5200A_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MHINSTEK_MHS_5200A_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "mhinstek-mhs-5200a"

#define PROTOCOL_LEN_MAX 32	/**< Max. line length for requests */
#define SERIAL_READ_TIMEOUT_MS 50
#define SERIAL_WRITE_TIMEOUT_MS 50

// don't change the values, these are returned by the function generator
enum attenuation_type {
	ATTENUATION_MINUS_20DB = 0,
	ATTENUATION_0DB        = 1,
};

// don't change the values, these are returned by the function generator
enum waveform_type {
	WAVEFORM_SINE = 0,
	WAVEFORM_SQUARE,
	WAVEFORM_TRIANGLE,
	WAVEFORM_RISING_SAWTOOTH,
	WAVEFORM_FALLING_SAWTOOTH,
	WAVEFORM_ARBITRARY_0 = 100,

	WAVEFORM_UNKNOWN = 1000,
};

enum waveform_options {
	WFO_FREQUENCY = 1,
	WFO_AMPLITUDE = 2,
	WFO_OFFSET = 4,
	WFO_PHASE = 8,
	WFO_DUTY_CYCLE = 16,
};

struct waveform_spec {
	enum waveform_type waveform;
	double freq_min;
	double freq_max;
	double freq_step;
	uint32_t opts;
};

struct channel_spec {
	const char *name;
	const struct waveform_spec *waveforms;
	uint32_t num_waveforms;
};

struct dev_context {
	struct sr_sw_limits limits;
	size_t buflen;
	double max_frequency; // for sine wave, all others are 6MHz
	uint8_t buf[PROTOCOL_LEN_MAX];

};

SR_PRIV int mhinstek_mhs_5200a_receive_data(int fd, int revents, void *cb_data);
SR_PRIV const char *mhinstek_mhs_5200a_waveform_to_string(enum waveform_type type);
SR_PRIV enum waveform_type mhinstek_mhs_5200a_string_to_waveform(const char *type);

#endif

/* Local Variables: */
/* mode: c */
/* indent-tabs-mode: t */
/* c-basic-offset: 8 */
/* tab-width: 8 */
/* End: */
