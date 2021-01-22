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

SR_PRIV const char *mhs5200a_waveform_to_string(enum waveform_type wtype)
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

SR_PRIV enum waveform_type mhs5200a_string_to_waveform(const char *wtype)
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

static void mhs5200a_send_channel_value(const struct sr_dev_inst *sdi,
					struct sr_channel *ch, double value, enum sr_mq mq,
					enum sr_unit unit, int digits)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	double val;

	val = value;
	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.meaning->channels = g_slist_append(NULL, ch);
	analog.num_samples = 1;
	analog.data = &val;
	analog.encoding->unitsize = sizeof(val);
	analog.encoding->is_float = TRUE;
	analog.encoding->digits = digits;
	analog.meaning->mq = mq;
	analog.meaning->unit = unit;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);
}

SR_PRIV int mhs5200a_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	GSList *l;
	int start_idx;
	int ret;
	double val;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	
	std_session_send_df_frame_begin(sdi);
	start_idx = 2;

	// frequency
	ret = mhs5200a_set_counter_function(sdi, COUNTER_MEASURE_FREQUENCY);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return FALSE;
	}
	ret = mhs5200a_get_counter_frequency(sdi, &val);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return SR_ERR;
	}
	l = g_slist_nth(sdi->channels, start_idx++);
	mhs5200a_send_channel_value(sdi, l->data, val, SR_MQ_FREQUENCY,
				    SR_UNIT_HERTZ, 10);

	// period
	ret = mhs5200a_set_counter_function(sdi, COUNTER_MEASURE_PERIOD);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return FALSE;
	}
	ret = mhs5200a_get_counter_period(sdi, &val);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return SR_ERR;
	}
	l = g_slist_nth(sdi->channels, start_idx++);
	mhs5200a_send_channel_value(sdi, l->data, val, SR_MQ_TIME,
				    SR_UNIT_SECOND, 10);
	
	// duty cycle
	ret = mhs5200a_set_counter_function(sdi, COUNTER_MEASURE_DUTY_CYCLE);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return FALSE;
	}
	ret = mhs5200a_get_counter_duty_cycle(sdi, &val);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return SR_ERR;
	}
	l = g_slist_nth(sdi->channels, start_idx++);
	mhs5200a_send_channel_value(sdi, l->data, val, SR_MQ_DUTY_CYCLE,
				    SR_UNIT_PERCENTAGE, 3);
	
	// pulse width
	ret = mhs5200a_set_counter_function(sdi, COUNTER_MEASURE_PULSE_WIDTH);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return FALSE;
	}
	ret = mhs5200a_get_counter_pulse_width(sdi, &val);
	if (ret < 0) {
		std_session_send_df_frame_end(sdi);
		return SR_ERR;
	}
	l = g_slist_nth(sdi->channels, start_idx++);
	mhs5200a_send_channel_value(sdi, l->data, val, SR_MQ_PULSE_WIDTH,
				    SR_UNIT_SECOND, 10);
	

	std_session_send_df_frame_end(sdi);
	sr_sw_limits_update_samples_read(&devc->limits, 1);

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}

/* Local Variables: */
/* mode: c */
/* indent-tabs-mode: t */
/* c-basic-offset: 8 */
/* tab-width: 8 */
/* End: */
