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

SR_PRIV int mhs_send_req(struct sr_serial_dev_inst *serial, const char *fmt, ...);

#define LINELEN_MAX 50	/**< Max. line length for requests */

#define REQ_TIMEOUT_MS 250 /**< Timeout [ms] for single request. */

enum waveform_type {
	WF_DC = 0,
	WF_SINE,
	WF_SQUARE,
	WF_TRIANGLE,
	WF_RISING_SAWTOOTH,
	WF_FALLING_SAWTOOTH,
};

enum waveform_options {
	WFO_FREQUENCY = 1,
	WFO_AMPLITUDE = 2,
	WFO_OFFSET = 4,
	WFO_PHASE = 8,
	WFO_DUTY_CYCLE = 16,
};

struct waveform_spec {
	const char *name;
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
  
	uint8_t buf[LINELEN_MAX];
	size_t buflen;

};

SR_PRIV int mhinstek_mhs_5200a_receive_data(int fd, int revents, void *cb_data);
SR_PRIV const char *mhinstek_mhs_5200a_waveform_to_string(enum waveform_type type);

#endif

/* Local Variables: */
/* mode: c */
/* indent-tabs-mode: t */
/* c-basic-offset: 8 */
/* tab-width: 8 */
/* End: */
