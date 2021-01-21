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

static struct sr_dev_driver mhinstek_mhs_5200a_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
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
	{ "Sine",         WF_SINE,     1.0E-6,  25.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "Square",       WF_SQUARE,   1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "Triangle",     WF_TRIANGLE,   1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "+ve Sawtooth", WF_RISING_SAWTOOTH,   1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "-ve Sawtooth", WF_FALLING_SAWTOOTH,   1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
};

static const struct channel_spec mhs5200a_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(mhs5200a_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(mhs5200a_waveforms) },
};

static const double phase_min_max_step[] = { 0.0, 360.0, 0.001 };

static gint64 calc_timeout_ms(gint64 start_us)
{
	gint64 result = REQ_TIMEOUT_MS - ((g_get_real_time() - start_us) / 1000);

	if (result < 0)
		return 0;

	return result;
}

/**
 * Read message into buf until "OK" received.
 *
 * @retval SR_OK Msg received; buf and buflen contain result, if any except OK.
 * @retval SR_ERR Error, including timeout.
*/
static int mhs_read_reply(struct sr_serial_dev_inst *serial, char **buf, int *buflen)
{
	gint64 timeout_start;

	*buf[0] = '\0';

	/* Read one line. It is either a data message or "OK". */
	timeout_start = g_get_real_time();
	if (serial_readline(serial, buf, buflen, calc_timeout_ms(timeout_start)) != SR_OK)
		return SR_ERR;
	if (!strcmp(*buf, "ok")) { /* We got an OK! */
		*buf[0] = '\0';
		*buflen = 0;
		return SR_OK;
	}
	return SR_OK;
}

/** Send command to device with va_list. */
static int mhs_send_va(struct sr_serial_dev_inst *serial, const char *fmt, va_list args)
{
	int retc;
	char auxfmt[LINELEN_MAX];
	char buf[LINELEN_MAX];

	snprintf(auxfmt, sizeof(auxfmt), "%s\r\n", fmt);
	vsnprintf(buf, sizeof(buf), auxfmt, args);

	sr_spew("mhs_send_va: \"%s\"", buf);

	retc = serial_write_blocking(serial, buf, strlen(buf),
			serial_timeout(serial, strlen(buf)));

	if (retc < 0)
		return SR_ERR;

	return SR_OK;
}

/** Send command to device. */
SR_PRIV int mhs_send_req(struct sr_serial_dev_inst *serial, const char *fmt, ...)
{
	int retc;
	va_list args;

	va_start(args, fmt);
	retc = mhs_send_va(serial, fmt, args);
	va_end(args);

	return retc;
}

/** Send command and consume simple OK reply. */
static int mhs_cmd_ok(struct sr_serial_dev_inst *serial, const char *fmt, ...)
{
	int retc;
	va_list args;
	char buf[LINELEN_MAX];
	char *bufptr;
	int buflen;

	/* Send command */
	va_start(args, fmt);
	retc = mhs_send_va(serial, fmt, args);
	va_end(args);

	if (retc != SR_OK)
		return SR_ERR;

	/* Read reply */
	buf[0] = '\0';
	bufptr = buf;
	buflen = sizeof(buf);
	retc = mhs_read_reply(serial, &bufptr, &buflen);
	if ((retc == SR_OK) && (buflen == 0))
		return SR_OK;

	return SR_ERR;
}

/**
 * Send command and read reply string.
 * @param reply Pointer to buffer of size LINELEN_MAX. Will be NUL-terminated.
 */
static int mhs_cmd_reply(char *reply, struct sr_serial_dev_inst *serial, const char *fmt, ...)
{
	int retc;
	va_list args;
	char buf[LINELEN_MAX];
	char *bufptr;
	int buflen;

	reply[0] = '\0';

	/* Send command */
	va_start(args, fmt);
	retc = mhs_send_va(serial, fmt, args);
	va_end(args);

	if (retc != SR_OK)
		return SR_ERR;

	/* Read reply */
	buf[0] = '\0';
	bufptr = buf;
	buflen = sizeof(buf);
	retc = mhs_read_reply(serial, &bufptr, &buflen);
	if ((retc == SR_OK) && (buflen > 0)) {
		strcpy(reply, buf);
		return SR_OK;
	}

	return SR_ERR;
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
	char tmp[16];
	char buf[LINELEN_MAX];

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
	if (mhs_cmd_reply(buf, serial, ":r0c") != SR_OK) {
		//serial_close(serial);
		//return NULL;
	} //XXXXXXXXXXXXXXXXX :r0c5225A5040000 */
	
	sr_info("Found device on port %s.", conn);

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("MHINSTEK");
	sdi->model = g_strdup("MHS-5200A");
	sdi->version = g_strdup("5.04"); //XXXXXXXXX firmware version
	sdi->serial_num = g_strdup("1234"); //XXXXXXXXXX serial number
	sdi->driver = &mhinstek_mhs_5200a_driver_info;

	
	devc = g_malloc0(sizeof(*devc));
	sr_sw_limits_init(&devc->limits);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;

	/* Create channel group and channel for each device channel. */
	ch_idx = 0;
	for (i = 0; i < ARRAY_SIZE(mhs5200a_channels); i++) {
		ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE,
				mhs5200a_channels[i].name);
		cg = g_malloc0(sizeof(*cg));
		snprintf(tmp, sizeof(tmp), "%u", i + 1);
		cg->name = g_strdup(tmp);
		cg->channels = g_slist_append(cg->channels, ch);

		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}
	
	/* Add found device to result set. */
	devices = g_slist_append(devices, sdi);

	serial_close(serial);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	fprintf(stderr, "%s: called\n", __func__);//XXXXXXXXXX
	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	fprintf(stderr, "%s: called\n", __func__);//XXXXXXXXXX
	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct sr_channel *ch;
	const struct channel_spec *ch_spec;
	GVariantBuilder *b;
	unsigned int i;
	double fspec[3];

	(void)sdi;
	(void)data;
	(void)cg;

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
						      mhinstek_mhs_5200a_waveform_to_string(ch_spec->waveforms[i].waveform));
			}
			*data = g_variant_new("as", b);
			g_variant_builder_unref(b);
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			/*
			 * Frequency range depends on the currently active
			 * wave form.
			 */
			fspec[0] = 1.0;
			fspec[1] = 200000;
			fspec[2] = 1;
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
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	fprintf(stderr, "%s: called\n", __func__);//XXXXXXXXXX
	(void)sdi;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */

	fprintf(stderr, "%s: called\n", __func__);//XXXXXXXXXX
	(void)sdi;

	return SR_OK;
}

static struct sr_dev_driver mhinstek_mhs_5200a_driver_info = {
	.name = "mhinstek-mhs-5200a",
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
SR_REGISTER_DEV_DRIVER(mhinstek_mhs_5200a_driver_info);

/* Local Variables: */
/* mode: c */
/* indent-tabs-mode: t */
/* c-basic-offset: 8 */
/* tab-width: 8 */
/* End: */
