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

#include <config.h>
#include "protocol.h"

SR_PRIV const char *mhinstek_mhs_5200a_waveform_to_string(enum waveform_type wtype)
{
	switch (wtype) {
	case WAVEFORM_SINE:
		return "sine";
	case WAVEFORM_SQUARE:
		return "square";
	case WAVEFORM_TRIANGLE:
		return "triangle";
	case WAVEFORM_RISING_SAWTOOTH:
		return "rising sawtooth";
	case WAVEFORM_FALLING_SAWTOOTH:
		return "falling sawtooth";
	case WAVEFORM_ARBITRARY_0:
		return "arbitrary waveform 0";
	case WAVEFORM_UNKNOWN:
		return "unknown";
	}
	return "unknown";
}

SR_PRIV enum waveform_type mhinstek_mhs_5200a_string_to_waveform(const char *wtype)
{
	if (!wtype)
		return WAVEFORM_UNKNOWN;
		
	if (g_ascii_strcasecmp(wtype, "sine") == 0) {
		return WAVEFORM_SINE;
	} else if (g_ascii_strcasecmp(wtype, "square") == 0) {
		return WAVEFORM_SQUARE;
	} else if (g_ascii_strcasecmp(wtype, "triangle") == 0) {
		return WAVEFORM_TRIANGLE;
	} else if (g_ascii_strcasecmp(wtype, "rising sawtooth") == 0) {
		return WAVEFORM_RISING_SAWTOOTH;
	} else if (g_ascii_strcasecmp(wtype, "falling sawtooth") == 0) {
		return WAVEFORM_FALLING_SAWTOOTH;
	} else {
		return WAVEFORM_UNKNOWN;
	}
}

SR_PRIV int mhinstek_mhs_5200a_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* TODO */
	}

	return TRUE;
}

/* Local Variables: */
/* mode: c */
/* indent-tabs-mode: t */
/* c-basic-offset: 8 */
/* tab-width: 8 */
/* End: */
