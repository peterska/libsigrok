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

#include <math.h>

#include <config.h>
#include "protocol.h"

static struct sr_dev_driver mhs5200a_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_SIGNAL_GENERATOR,
};

static const uint32_t mhs5200a_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t mhs5200a_devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AMPLITUDE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OFFSET | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PHASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DUTY_CYCLE | SR_CONF_GET | SR_CONF_SET,
};

#define WAVEFORM_DEFAULT WFO_FREQUENCY | WFO_AMPLITUDE | WFO_OFFSET | WFO_PHASE

static const struct waveform_spec mhs5200a_waveforms[] = {
	{ WAVEFORM_SINE,             1.0E-6,  21.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ WAVEFORM_SQUARE,           1.0E-6,   6.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ WAVEFORM_TRIANGLE,         1.0E-6,   6.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ WAVEFORM_RISING_SAWTOOTH,  1.0E-6,   6.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ WAVEFORM_FALLING_SAWTOOTH, 1.0E-6,   6.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
};

static const struct channel_spec mhs5200a_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(mhs5200a_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(mhs5200a_waveforms) },
};

static const double phase_min_max_step[] = { 0.0, 360.0, 1.0 };


/**
 * Read message into buf until "OK" received.
 *
 * @retval SR_OK Msg received; buf and buflen contain result, if any except OK.
 * @retval SR_ERR Error, including timeout.
*/
static int mhs5200a_read_reply(struct sr_serial_dev_inst *serial, char **buf, int *buflen)
{
	int retc;
	
	*buf[0] = '\0';

	retc = serial_read_blocking(serial, *buf, *buflen, SERIAL_READ_TIMEOUT_MS);
	if (retc <= 0)
		return SR_ERR;
	while ( ((*buf)[retc - 1] == '\n') || ((*buf)[retc - 1] == '\r') ) {
		(*buf)[retc - 1] = '\0';
		--retc;
	}
	*buflen = retc;
	if (!strcmp(*buf, "ok")) { /* We got an OK! */
		*buf[0] = '\0';
		*buflen = 0;
		return SR_OK;
	}
	return SR_OK;
}

/** Send command to device with va_list. */
static int mhs5200a_send_va(struct sr_serial_dev_inst *serial, const char *fmt, va_list args)
{
	int retc;
	char auxfmt[PROTOCOL_LEN_MAX];
	char buf[PROTOCOL_LEN_MAX];

	snprintf(auxfmt, sizeof(auxfmt), "%s\n", fmt); // all command require a \n at the end
	vsnprintf(buf, sizeof(buf), auxfmt, args);

	retc = serial_write_blocking(serial, buf, strlen(buf), SERIAL_WRITE_TIMEOUT_MS);
	if (retc < 0)
		return SR_ERR;

	return SR_OK;
}

/** Send command and consume simple OK reply. */
static int mhs5200a_cmd_ok(struct sr_serial_dev_inst *serial, const char *fmt, ...)
{
	int retc;
	va_list args;
	char buf[PROTOCOL_LEN_MAX];
	char *bufptr;
	int buflen;

	/* Send command */
	va_start(args, fmt);
	retc = mhs5200a_send_va(serial, fmt, args);
	va_end(args);

	if (retc != SR_OK)
		return SR_ERR;

	/* Read reply */
	buf[0] = '\0';
	bufptr = buf;
	buflen = sizeof(buf);
	retc = mhs5200a_read_reply(serial, &bufptr, &buflen);
	if ((retc == SR_OK) && (buflen == 0))
		return SR_OK;

	return SR_ERR;
}

/**
 * Send command and read reply string.
 * @param reply Pointer to buffer of size PROTOCOL_LEN_MAX. Will be NUL-terminated.
 */
static int mhs5200a_cmd_reply(char *reply, struct sr_serial_dev_inst *serial, const char *fmt, ...)
{
	int retc;
	va_list args;
	char buf[PROTOCOL_LEN_MAX];
	char *bufptr;
	int buflen;

	reply[0] = '\0';

	/* Send command */
	va_start(args, fmt);
	retc = mhs5200a_send_va(serial, fmt, args);
	va_end(args);

	if (retc != SR_OK)
		return SR_ERR;

	/* Read reply */
	buf[0] = '\0';
	bufptr = buf;
	buflen = sizeof(buf);
	retc = mhs5200a_read_reply(serial, &bufptr, &buflen);
	if ((retc == SR_OK) && (buflen > 0)) {
		strcpy(reply, buf);
		return SR_OK;
	}

	return SR_ERR;
}

static int mhs5200a_get_waveform(const struct sr_dev_inst *sdi, int ch, long *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%dw", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atol(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int mhs5200a_get_attenuation(const struct sr_dev_inst *sdi, int ch, long *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%dy", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atol(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int mhs5200a_get_onoff(const struct sr_dev_inst *sdi, long *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r1b");
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atol(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int mhs5200a_get_frequency(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%df", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	*val /= 100.0;
	return SR_OK;
}

static int mhs5200a_get_amplitude(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	long attenuation;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_get_attenuation(sdi, ch, &attenuation);
	if (retc < 0)
		return SR_ERR;
	
	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%da", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	*val /= 100.0;
	if (attenuation == ATTENUATION_MINUS_20DB) {
		*val /= 10.0;
	}
	return SR_OK;
}

static int mhs5200a_get_duty_cycle(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%dd", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	*val /= 10.0;
	return SR_OK;
}

static int mhs5200a_get_offset(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	double amplitude;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_get_amplitude(sdi, ch, &amplitude);
	if (retc < 0)
		return SR_ERR;

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%do", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	*val -= 120.0; // offset is returned as a percentage of amplitude
	*val = amplitude * *val / 100.0;
	return SR_OK;
}

static int mhs5200a_get_phase(const struct sr_dev_inst *sdi, int ch, double *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r%dp", ch);
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int mhs5200a_set_frequency(const struct sr_dev_inst *sdi, int ch, double val)
{
	const struct waveform_spec *wspec;
	struct dev_context *devc;
	long wtype;
	unsigned int i;
	
	if (mhs5200a_get_waveform(sdi, ch, &wtype) < 0) {
		return SR_ERR;
	}
	devc = sdi->priv;
	wspec = NULL;
	for (i = 0; i < ARRAY_SIZE(mhs5200a_waveforms); i++) {
		if (mhs5200a_waveforms[i].waveform == wtype) {
			wspec = &mhs5200a_waveforms[i];
			break;
		}
	}
	if (!wspec) {
		sr_err("Could not determine current pattern type");
		return SR_ERR;
	}
	
	if (val > devc->max_frequency || val < wspec->freq_min || val > wspec->freq_max) {
		sr_err("Invalid frequency %.2fHz for %s wave. Valid values are between %.2fHZ and %.2fHz",
		       val, mhs5200a_waveform_to_string(wtype),
		       wspec->freq_min, wspec->freq_max);
		return SR_ERR;
	}
	return mhs5200a_cmd_ok(sdi->conn, ":s%df%d", ch, (int)(val * 100.0 + 0.5));
}

static int mhs5200a_set_waveform(const struct sr_dev_inst *sdi, int ch, long val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s%dw%d", ch, val);
}

static int mhs5200a_set_waveform_string(const struct sr_dev_inst *sdi, int ch, const char *val)
{
	enum waveform_type wtype;
	
	wtype = mhs5200a_string_to_waveform(val);
	if (wtype == WAVEFORM_UNKNOWN) {
		sr_err("Unknown waveform %s", val);
		return SR_ERR;
	}
	return mhs5200a_set_waveform(sdi, ch, wtype);
}

static int mhs5200a_set_amplitude(const struct sr_dev_inst *sdi, int ch, double val)
{
	long attenuation;

	if (val < 0.0 || val > 20.0) {
		sr_err("Invalid amplitude %.2fV. Supported values are between 0V and 20V", val);
		return SR_ERR;
	}
	if (mhs5200a_get_attenuation(sdi, ch, &attenuation) < 0)
		return SR_ERR;
	
	if (attenuation == ATTENUATION_MINUS_20DB) {
		val *= 1000.0;
	}  else {
		val *= 100.0;
	}
	return mhs5200a_cmd_ok(sdi->conn, ":s%da%d", ch, (int)(val + 0.5));
}

static int mhs5200a_set_duty_cycle(const struct sr_dev_inst *sdi, int ch, double val)
{
	if (val < 0.0 || val > 100.0) {
		sr_err("Invalid duty cycle %.2f%%. Supported values are between 0%% and 100%%", val);
		return SR_ERR;
	}
	return mhs5200a_cmd_ok(sdi->conn, ":s%dd%d", ch, (int)(val * 10.0 + 0.5));
}

static int mhs5200a_set_offset(const struct sr_dev_inst *sdi, int ch, double val)
{
	double amplitude;

	if (mhs5200a_get_amplitude(sdi, ch, &amplitude) < 0)
		return SR_ERR;
	// offset is set as a percentage and encoded with an offset of 120 for a range of -120 to 120
	val = val / amplitude * 100.0;
	if (val > 120.0 || val < -120.0) {
		sr_err("Invalid offset %.2f%%. Supported values are between -120%% and 120%% of the amplitude value", val);
		return SR_ERR;
	}
	return mhs5200a_cmd_ok(sdi->conn, ":s%do%d", ch, (int)(val + 0.5 + 120.0));
}

static int mhs5200a_set_phase(const struct sr_dev_inst *sdi, int ch, double val)
{
	while (val >= 360.0)
		val -= 360.0;
	while (val < 0.0)
		val += 360.0;
			
	return mhs5200a_cmd_ok(sdi->conn, ":s%dp%d", ch, (int)(val + 0.5));
}

/*static int mhs5200a_set_attenuation(const struct sr_dev_inst *sdi, int ch, long val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s%dy%d", ch, val);
	}*/

static int mhs5200a_set_onoff(const struct sr_dev_inst *sdi, long val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s1b%d", val ? 1 : 0);
}

static int mhs5200a_set_counter_onoff(const struct sr_dev_inst *sdi, long val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s6b%d", val);
}

SR_PRIV  int mhs5200a_set_counter_function(const struct sr_dev_inst *sdi, enum counter_function val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s%dm", val);
}

SR_PRIV int mhs5200a_set_counter_gate_time(const struct sr_dev_inst *sdi, enum gate_time val)
{
	return mhs5200a_cmd_ok(sdi->conn, ":s1g%d", val);
}

SR_PRIV int mhs5200a_get_counter_value(const struct sr_dev_inst *sdi, double *val)
{
	int retc;
	char buf[PROTOCOL_LEN_MAX];

	retc = mhs5200a_cmd_reply(buf, sdi->conn, ":r0e");
	if (retc < 0)
		return SR_ERR;

	if (strlen(buf) < 4)
		return SR_ERR;

	if (sr_atod(buf + 4, val) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int mhs5200a_get_counter_frequency(const struct sr_dev_inst *sdi, double *val)
{
	if (mhs5200a_get_counter_value(sdi, val) < 0)
		return SR_ERR;

	*val /= 10.0;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_counter_period(const struct sr_dev_inst *sdi, double *val)
{
	if (mhs5200a_get_counter_value(sdi, val) < 0)
		return SR_ERR;

	*val *= 1.0e-9;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_counter_pulse_width(const struct sr_dev_inst *sdi, double *val)
{
	if (mhs5200a_get_counter_value(sdi, val) < 0)
		return SR_ERR;

	*val *= 1.0e-9;
	return SR_OK;
}

SR_PRIV int mhs5200a_get_counter_duty_cycle(const struct sr_dev_inst *sdi, double *val)
{
	if (mhs5200a_get_counter_value(sdi, val) < 0)
		return SR_ERR;

	*val /= 10.0;
	return SR_OK;
}


static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_config *src;
	const char *conn, *serialcomm;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	GSList *l, *devices;
	unsigned int i, ch_idx;
	char buf[PROTOCOL_LEN_MAX];
	char model[PROTOCOL_LEN_MAX];

	devices = NULL;
	conn = NULL;
	serialcomm = "57600/8n1";
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;
	
	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	sr_info("Probing serial port %s.", conn);

	/* Query and verify model string. */
	if ( (mhs5200a_cmd_reply(buf, serial, ":r0c") != SR_OK) || strlen(buf) < 10) {
		serial_close(serial);
		return NULL;
	}
	if (strncmp(buf, ":r0c52", 6) != 0) {
		serial_close(serial);
		return NULL;
	}
	strcpy(model, "MHS-");
	strncpy(model + 4, buf + 4, 5);
	model[9] = 0;
	
	sr_info("Found device on port %s.", conn);

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("MHINSTEK");
	sdi->model = g_strdup(model);
	//sdi->version = g_strdup("5.04");
	//sdi->serial_num = g_strdup("1234");
	sdi->driver = &mhs5200a_driver_info;

	
	devc = g_malloc0(sizeof(*devc));
	sr_sw_limits_init(&devc->limits);
	devc->max_frequency = (model[6] - '0') * 10 + model[7] - '0';
	devc->max_frequency *= 1.0e06;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;

	/* Create channel group and channel for each device channel. */
	ch_idx = 0;
	for (i = 0; i < ARRAY_SIZE(mhs5200a_channels); i++) {
		ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE,
				mhs5200a_channels[i].name);
		cg = g_malloc0(sizeof(*cg));
		snprintf(buf, sizeof(buf), "%u", i + 1);
		cg->name = g_strdup(buf);
		cg->channels = g_slist_append(cg->channels, ch);

		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}
	
	/* Create channels for the frequency counter output. */
	ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE, "FREQ1");
	ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE, "PERIOD1");
	ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE, "DUTY1");
	ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE, "WIDTH1");

	/* Add found device to result set. */
	devices = g_slist_append(devices, sdi);

	serial_close(serial);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	const struct sr_key_info *kinfo;
	double valf;
	long vall;
	int ret;
	
	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;
	ret = SR_OK;
	kinfo = sr_key_info_get(SR_KEY_CONFIG, key);

	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_SAMPLES:
		case SR_CONF_LIMIT_MSEC:
			ret = sr_sw_limits_config_get(&devc->limits, key, data);
			break;
		default:
			sr_dbg("%s: Unsupported key: %d (%s)", __func__,
				(int)key, (kinfo ? kinfo->name : "unknown"));
			ret = SR_ERR_NA;
			break;
		}
	} else {
		ch = cg->channels->data;

		switch (key) {
		case SR_CONF_ENABLED:
			if ((ret = mhs5200a_get_onoff(sdi, &vall)) == SR_OK) {
				*data = g_variant_new_boolean(vall);
			}
			break;
		case SR_CONF_PATTERN_MODE:
			if ((ret = mhs5200a_get_waveform(sdi, ch->index + 1, &vall)) == SR_OK) {
				*data = g_variant_new_string(mhs5200a_waveform_to_string(vall));
			}
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			if ((ret = mhs5200a_get_frequency(sdi, ch->index + 1, &valf)) == SR_OK)
				*data = g_variant_new_double(valf);
			break;
		case SR_CONF_AMPLITUDE:
			if ((ret = mhs5200a_get_amplitude(sdi, ch->index + 1, &valf)) == SR_OK)
				*data = g_variant_new_double(valf);
			break;
		case SR_CONF_OFFSET:
			if ((ret = mhs5200a_get_offset(sdi, ch->index + 1, &valf)) == SR_OK)
				*data = g_variant_new_double(valf);
			break;
		case SR_CONF_PHASE:
			if ((ret = mhs5200a_get_phase(sdi, ch->index + 1, &valf)) == SR_OK)
				*data = g_variant_new_double(valf);
			break;
		case SR_CONF_DUTY_CYCLE:
			if ((ret = mhs5200a_get_duty_cycle(sdi, ch->index + 1, &valf)) == SR_OK)
				*data = g_variant_new_double(valf);
			break;
		default:
			sr_dbg("%s: Unsupported (cg) key: %d (%s)", __func__,
				(int)key, (kinfo ? kinfo->name : "unknown"));
			ret = SR_ERR_NA;
			break;
		}
	}
		
	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	const struct sr_key_info *kinfo;
	int ret;
	
	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;
	ret = SR_OK;
	kinfo = sr_key_info_get(SR_KEY_CONFIG, key);

	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_SAMPLES:
		case SR_CONF_LIMIT_MSEC:
			ret = sr_sw_limits_config_set(&devc->limits, key, data);
			break;
		default:
			sr_dbg("%s: Unsupported key: %d (%s)", __func__,
				(int)key, (kinfo ? kinfo->name : "unknown"));
			ret = SR_ERR_NA;
			break;
		}
	} else {
		ch = cg->channels->data;

		switch (key) {
		case SR_CONF_ENABLED:
			ret = mhs5200a_set_onoff(sdi, g_variant_get_boolean(data));
			break;
		case SR_CONF_PATTERN_MODE:
			ret = mhs5200a_set_waveform_string(sdi, ch->index + 1, g_variant_get_string(data, NULL));
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			ret = mhs5200a_set_frequency(sdi, ch->index + 1, g_variant_get_double(data));
			break;
		case SR_CONF_AMPLITUDE:
			ret = mhs5200a_set_amplitude(sdi, ch->index + 1, g_variant_get_double(data));
			break;
		case SR_CONF_OFFSET:
			ret = mhs5200a_set_offset(sdi, ch->index + 1, g_variant_get_double(data));
			break;
		case SR_CONF_PHASE:
			ret = mhs5200a_set_phase(sdi, ch->index + 1, g_variant_get_double(data));
			break;
		case SR_CONF_DUTY_CYCLE:
			ret = mhs5200a_set_duty_cycle(sdi, ch->index + 1, g_variant_get_double(data));
			break;
		default:
			sr_dbg("%s: Unsupported (cg) key: %d (%s)", __func__,
			       (int)key, (kinfo ? kinfo->name : "unknown"));
			ret = SR_ERR_NA;
			break;
		}
	}
		
	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	const struct channel_spec *ch_spec;
	GVariantBuilder *b;
	unsigned int i;
	double fspec[3];

	if (sdi) {
		devc = sdi->priv;
	} else {
		devc = NULL;
	}
	
	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return std_opts_config_list(key, data, sdi, cg,
						    ARRAY_AND_SIZE(scanopts),
						    ARRAY_AND_SIZE(drvopts),
						    ARRAY_AND_SIZE(mhs5200a_devopts));
		default:
			return SR_ERR_NA;
		}
	} else {
		ch = cg->channels->data;
		ch_spec = &mhs5200a_channels[ch->index];
		switch(key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(mhs5200a_devopts_cg));
			break;
		case SR_CONF_PATTERN_MODE:
			b = g_variant_builder_new(G_VARIANT_TYPE("as"));
			for (i = 0; i < ch_spec->num_waveforms; i++) {
				g_variant_builder_add(b, "s",
						      mhs5200a_waveform_to_string(ch_spec->waveforms[i].waveform));
			}
			*data = g_variant_new("as", b);
			g_variant_builder_unref(b);
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			if (!devc) {
				return SR_ERR;
			}
			fspec[0] = 0.1;
			fspec[1] = devc->max_frequency;
			fspec[2] = 0.1;
			*data = std_gvar_min_max_step_array(fspec);
			break;
		case SR_CONF_PHASE:
			*data = std_gvar_min_max_step_array(phase_min_max_step);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	
	if (!sdi)
		return SR_ERR_ARG;


	ret = mhs5200a_set_counter_function(sdi, COUNTER_MEASURE_FREQUENCY);
	if (ret < 0)
		return SR_ERR;
	
	ret = mhs5200a_set_counter_onoff(sdi, 1);
	if (ret < 0)
		return SR_ERR;
	
	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	ret = std_session_send_df_header(sdi);
	sr_session_source_add(sdi->session, -1, 0, 1000, mhs5200a_receive_data, (void *)sdi);
	return ret;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	if (!sdi)
		return SR_ERR_ARG;

	mhs5200a_set_counter_onoff(sdi, 0);
	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);
	return SR_OK;
}

static struct sr_dev_driver mhs5200a_driver_info = {
	.name = "mhs-5200a",
	.longname = "MHINSTEK MHS-5200A",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(mhs5200a_driver_info);

/* Local Variables: */
/* mode: c */
/* indent-tabs-mode: t */
/* c-basic-offset: 8 */
/* tab-width: 8 */
/* End: */
