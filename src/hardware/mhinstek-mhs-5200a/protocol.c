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

SR_PRIV const char *mhinstek_mhs_5200a_waveform_to_string(enum waveform_type type)
{
	switch (type) {
	case WF_DC:
		return "DC";
	case WF_SINE:
		return "Sine";
	case WF_SQUARE:
		return "Square";
	case WF_TRIANGLE:
		return "Triangle";
	case WF_RISING_SAWTOOTH:
		return "Rising Sawtooth";
	case WF_FALLING_SAWTOOTH:
		return "Falling Sawtooth";
	}

	return "Unknown";
}

SR_PRIV int mhinstek_mhs_5200a_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	fprintf(stderr, "%s: called\n", __func__);//XXXXXXXXXX
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
