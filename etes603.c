/*
 * EgisTec ES603 driver for libfprint
 * Copyright (C) 2012 Patrick Marlier
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * Thanks to all testers with special mention to:
 *   Chase Montgomery (BLuFeNiX)
 *   Matthias Macha
 */

/* EgisTec ES603 device information
 *   Sensor area: 192 x 4 pixels
 *   Sensor gray: 16 gray levels/sensor pixel
 *   Sensor resolution: 508 dpi
 *   USB Manufacturer ID: 1C7A
 *   USB Product ID: 0603
 *
 * Possible compatibility LTT-SS500/SS501 (except 16 pixel instead of 4) ?
 *
 * To log communication with the sensor, just define DEBUG_TRANSFER.
 * #define DEBUG_TRANSFER
 */

/* TODO LIST
 *   Detect sweep direction (support only one direction currently)
 *   Use different ways to detect fingers
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <sys/time.h>
#include <libusb.h>

#define FP_COMPONENT "etes603"
#include <fp_internal.h>
#include <drivers/driver_ids.h>

/* libusb defines */
#define EP_IN              0x81
#define EP_OUT             0x02
#define BULK_TIMEOUT       1000 /* Note that 1000 ms is usually enough but with CMD_READ_FP could be longer since the sensor is waiting motion. */

/* es603 defines */
#define FRAME_WIDTH        192  /* pixels per row */
#define FRAME_HEIGHT       4    /* number of rows */
#define FRAME_SIZE         384  /* size in bytes: FRAME_WIDTH * FRAME_HEIGH / 2 pixels per byte */
#define FRAMEFP_WIDTH      256  /* pixels per row */
#define FRAMEFP_HEIGHT     500  /* number of rows */
#define FRAMEFP_SIZE       64000 /* size in bytes: width * height / 2 pixels per byte */

#define GAIN_SMALL_INIT    0x23 /* Initial small gain */
#define VRT_MAX	           0x3F /* Maximum value for VRT */
#define VRB_MAX            0x3A /* Maximum value for VRB */
#define DTVRT_MAX          0x3A /* Maximum value for DTVRT */
#define DCOFFSET_MIN       0x00 /* Minimum value for DCoffset */
#define DCOFFSET_MAX       0x35 /* Maximum value for DCoffset */

/* es603 commands */
#define CMD_READ_REG       0x01
#define CMD_WRITE_REG      0x02
#define CMD_READ_FRAME     0x03 /* Read the sensor area */
#define CMD_READ_FP        0x06 /* Read a fingerprint */
#define CMD_20             0x20 /* ? */
#define CMD_25             0x25 /* ? */
#define CMD_60             0x60 /* ? */

#define CMD_OK             0x01 /* Command successfully executed */

/* es603 registers */
#define REG_MAX            0x18 /* Maximum number of registers in one message */
#define REG_MODE_CONTROL   0x02 /* Mode control */
#define REG_03             0x03 /* Contact register? */
#define REG_04             0x04 /* ? */
#define REG_10             0x10 /* MVS FRMBUF control */
#define REG_1A             0x1A /* ? */
/* BEGIN init sensor */
#define REG_20             0x20 /* (def: 0x00) */
#define REG_21             0x21 /* Small gain (def: 0x23) */
#define REG_22             0x22 /* Normal gain (def: 0x21) */
#define REG_23             0x23 /* Large gain (def: 0x20) */
#define REG_24             0x24 /* (def: 0x14) */
#define REG_25             0x25 /* (def: 0x6A) */
#define REG_26             0x26 /* VRB again? (def: 0x00) */
#define REG_27             0x27 /* VRT again? (def: 0x00) */
#define REG_28             0x28 /* (def: 0x00) */
#define REG_29             0x29 /* (def: 0xC0) */
#define REG_2A             0x2A /* (def: 0x50) */
#define REG_2B             0x2B /* (def: 0x50) */
#define REG_2C             0x2C /* (def: 0x4D) */
#define REG_2D             0x2D /* (def: 0x03) */
#define REG_2E             0x2E /* (def: 0x06) */
#define REG_2F             0x2F /* (def: 0x06) */
#define REG_30             0x30 /* (def: 0x10) */
#define REG_31             0x31 /* (def: 0x02) */
#define REG_32             0x32 /* (def: 0x14) */
#define REG_33             0x33 /* (def: 0x34) */
#define REG_34             0x34 /* (def: 0x01) */
#define REG_35             0x35 /* (def: 0x08) */
#define REG_36             0x36 /* (def: 0x03) */
#define REG_37             0x37 /* (def: 0x21) */
/* END init sensor */

#define REG_ENC1           0x41 /* Encryption 1 */
#define REG_ENC2           0x42
#define REG_ENC3           0x43
#define REG_ENC4           0x44
#define REG_ENC5           0x45
#define REG_ENC6           0x46
#define REG_ENC7           0x47
#define REG_ENC8           0x48 /* Encryption 8 */

#define REG_50             0x50 /* ? For contact detection */
#define REG_51             0x51 /* ? */
#define REG_59             0x59 /* ? */
#define REG_5A             0x5A /* ? */
#define REG_5B             0x5B /* ? */

#define REG_INFO0          0x70 /* Sensor model byte0 */
#define REG_INFO1          0x71 /* Sensor model byte1 */
#define REG_INFO2          0x72 /* Sensor model byte2 */
#define REG_INFO3          0x73 /* Sensor model byte3 */

#define REG_93             0x93 /* ? */
#define REG_94             0x94 /* ? */

#define REG_GAIN           0xE0
#define REG_VRT            0xE1
#define REG_VRB            0xE2
#define REG_DTVRT          0xE3 /* used for contact detection */
#define REG_VCO_CONTROL    0xE5 /* 0x13 (IDLE?), 0x14 (REALTIME) */
#define REG_DCOFFSET       0xE6

#define REG_F0             0xF0 /* ? init:0x00 close:0x01 */
#define REG_F2             0xF2 /* ? init:0x00 close:0x4E */

#define REG_MODE_SLEEP     0x30 /* Sleep mode */
#define REG_MODE_CONTACT   0x31 /* Contact mode */
#define REG_MODE_SENSOR    0x33 /* Sensor mode */
#define REG_MODE_FP        0x34 /* FingerPrint mode (Fly-Estimation®) */

#define REG_VCO_IDLE       0x13
#define REG_VCO_RT         0x14 /* Realtime */

/* Contact sensor parameters */
#define CS_DETECT_TIMEOUT  5000 /* Waiting time to detect contact (ms) */
#define CS_DETECT_DELAY    5    /* Delay between each test (ms) */

/* This structure must be packed because it is a the raw message sent. */
struct egis_msg {
	uint8_t magic[5]; /* out: 'EGIS' 0x09 / in: 'SIGE' 0x0A */
	uint8_t cmd;
	union {
		struct {
			uint8_t nb;
			uint8_t regs[REG_MAX];
		} egis_readreg;
		struct {
			uint8_t regs[REG_MAX];
		} sige_readreg;
		struct {
			uint8_t nb;
			struct {
				uint8_t reg;
				uint8_t val;
			} regs[REG_MAX];
		} egis_writereg;
		struct {
			uint8_t length_factor;
			uint8_t length;
			uint8_t use_gvv;
			uint8_t gain;
			uint8_t vrt;
			uint8_t vrb;
		} egis_readf;
		struct {
			uint8_t len[2];
			uint8_t val[3];
		} egis_readfp;
		struct {
			uint8_t val[5];
		} sige_misc;
		uint8_t padding[0x40-6]; /* Ensure size of 0x40 */
	};
} __attribute__((packed));


/* Structure to keep information between asynchronous functions. */
struct etes603_dev {
	libusb_device_handle *udev;
	uint8_t gain;
	uint8_t dcoffset;
	uint8_t vrt;
	uint8_t vrb;
	/* Realtime capture and contact detection may required different
	 * DCOffset. */
	uint8_t dcoffset_ct;
	uint8_t dtvrt;
	/* TODO can probably keep registers value, particularly control_mode */

	/* Asynchronous fields */
	unsigned int deactivating; /* TODO could be merge with state? */
	unsigned int state;
	unsigned int mode; /* FingerPrint mode (0) or merging frames (1) */
	uint8_t *braw; /* Pointer to raw buffer */
	uint8_t *braw_cur; /* Current position in the raw buffer */
	uint8_t *braw_end; /* End of the raw buffer */
};

/* Forward declarations */
static uint8_t *process_frame(uint8_t *dst, uint8_t *src);
static int process_frame_empty(uint8_t *f, size_t s, int mode);
static int contact_detect(struct etes603_dev *dev);

#ifdef DEBUG_TRANSFER
static void debug_output(unsigned char ep, uint8_t *data, size_t size) {
	unsigned int i;
	static FILE * fdebug = NULL;

	if (fdebug == NULL) {
		if ((fdebug = fopen("/tmp/etes603", "w")) == NULL) {
			fp_dbg("Cannot open file /tmp/etes6030 (errno=%d)", errno);
			fdebug = (void *)~0;
		}
	}
	if (fdebug == (void *)~0) {
		return;
	}

	if (ep == EP_OUT)
		fprintf(fdebug, ">>> %lu bytes\n", size);
	else if (ep == EP_IN)
		fprintf(fdebug, "<<< %lu bytes\n", size);

	for (i = 0; i < size; i++) {
		if (i != 0 && i % 16 == 0)
			fwrite("\n", 1, 1, fdebug);
		fprintf(fdebug, "%02X ", data[i]);
	}
	fwrite("\n", 1, 1, fdebug);
}
#else
# define debug_output(...)
#endif

/*
 * Transfer (in/out) egis command to the device using synchronous libusb.
 */
static int sync_transfer(libusb_device_handle *udev, unsigned char ep,
		struct egis_msg *msg, unsigned int size)
{
	int ret, actual_length;
	unsigned char *data = (unsigned char *)msg;

	assert(udev != NULL);

	ret = libusb_bulk_transfer(udev, ep, data, size, &actual_length,
				   BULK_TIMEOUT);

	if (ret < 0) {
		fp_err("Bulk write error %s (%d)", libusb_error_name(ret), ret);
		return -EIO;
	}
	debug_output(ep, data, actual_length);

	return actual_length;
}

/* The size of the message header is 5 plus 1 for the command. */
#define MSG_HDR_SIZE 6

/*
 * Prepare the header of the message to be sent to the device.
 */
static void msg_header_prepare(struct egis_msg *msg)
{
	msg->magic[0] = 'E';
	msg->magic[1] = 'G';
	msg->magic[2] = 'I';
	msg->magic[3] = 'S';
	msg->magic[4] = 0x09;
}

/*
 * Check that the header of the received message is correct.
 */
static int msg_header_check(struct egis_msg *msg)
{
	if (msg->magic[0] == 'S' && msg->magic[1] == 'I'
	    && msg->magic[2] == 'G' && msg->magic[3] == 'E'
	    && msg->magic[4] == 0x0A)
		return 0;
	return -1;
}

/*
 * Prepare message to ask for a frame.
 */
static void msg_get_frame(struct egis_msg *msg, uint8_t length,
	uint8_t use_gvv, uint8_t gain, uint8_t vrt, uint8_t vrb)
{
	msg_header_prepare(msg);
	msg->cmd = CMD_READ_FRAME;
	msg->egis_readf.length_factor = 0x01;
	/* length should be 0xC0 */
	msg->egis_readf.length = length;
	msg->egis_readf.use_gvv = use_gvv;
	/* if use_gvv is set, gain/vrt/vrb are used */
	msg->egis_readf.gain = gain;
	msg->egis_readf.vrt = vrt;
	msg->egis_readf.vrb = vrb;
}

/*
 * Prepare message to ask for a fingerprint frame.
 */
static void msg_get_fp(struct egis_msg *msg, uint8_t len0, uint8_t len1,
	uint8_t v2, uint8_t v3, uint8_t v4)
{
	msg_header_prepare(msg);
	msg->cmd = CMD_READ_FP;
	/* Unknown values and always same on captured frames.
	 * 1st 2nd bytes is unsigned short for height, but only on value range
	 * 0x01 0xF4 (500), 0x02 0x00 (512), 0x02 0xF4 (756) are ok
	 */
	msg->egis_readfp.len[0] = len0;
	msg->egis_readfp.len[1] = len1;
	/* 3rd byte : ?? but changes frame size
	 * 4th byte : 0x00 -> can change width
	 * 5th byte : motion sensibility?
	 */
	msg->egis_readfp.val[0] = v2;
	msg->egis_readfp.val[1] = v3;
	msg->egis_readfp.val[2] = v4;
}

/*
 * Ask synchronously the sensor for a frame.
 * if use_gvv is 0, gain/vrt/vrb are ineffective.
 */
static int dev_get_frame(libusb_device_handle *udev, uint8_t length,
	uint8_t use_gvv, uint8_t gain, uint8_t vrt, uint8_t vrb, uint8_t *buf)
{
	struct egis_msg msg;
	int ret;
	unsigned int i, fsize = length * 2;

	msg_get_frame(&msg, length, use_gvv, gain, vrt, vrb);

	ret = sync_transfer(udev, EP_OUT, &msg, MSG_HDR_SIZE + 6);
	if (ret < 0) {
		fp_err("sync_transfer EP_OUT failed");
		goto err;
	}

	for (i = 0 ; i < fsize; i += ret) {
		ret = sync_transfer(udev, EP_IN, (struct egis_msg *)(buf + i),
				    fsize - i);
		if (ret < 0) {
			fp_err("sync_transfer EP_IN failed");
			goto err;
		}
	}

	return 0;
err:
	return -1;
}

/*
 * Ask synchronously the sensor for a fingerprint.
 */
static int dev_get_fp(libusb_device_handle *udev, uint8_t *buf)
{
	struct egis_msg msg;
	int ret, i;

	msg_get_fp(&msg, 0x01, 0xF4, 0x02, 0x01, 0x64);

	ret = sync_transfer(udev, EP_OUT, &msg, MSG_HDR_SIZE + 5);
	if (ret < 0) {
		fp_err("sync_transfer EP_OUT failed");
		goto err;
	}
	for (i = 0 ; i < FRAMEFP_SIZE; i += ret) {
		ret = sync_transfer(udev, EP_IN, (struct egis_msg *)(buf + i),
				    FRAMEFP_SIZE - i);
		if (ret < 0) {
			fp_err("sync_transfer EP_IN failed");
			goto err;
		}
	}

	return 0;
err:
	return -1;
}

/*
 * Read synchronously a register from the sensor.
 * Variadic argument pattern: int reg, int *val, ...
 */
static int dev_get_regs(libusb_device_handle *udev, int n_args, ... )
{
	va_list ap;
	struct egis_msg msg;
	int ret, i;

	if (n_args == 0 || n_args % 2 != 0 || n_args > REG_MAX * 2) {
		fp_err("wrong number of arguments (%d)", n_args);
		goto err;
	}

	msg_header_prepare(&msg);
	msg.cmd = CMD_READ_REG;
	msg.egis_readreg.nb = n_args / 2;

	va_start(ap, n_args);
	for (i = 0; i < n_args / 2; i++) {
		msg.egis_readreg.regs[i] = va_arg(ap, int);
		va_arg(ap, uint8_t*);
	}
	va_end(ap);

	ret = sync_transfer(udev, EP_OUT, &msg, MSG_HDR_SIZE + 1 + n_args / 2);
	if (ret < 0) {
		fp_err("sync_transfer EP_OUT failed");
		goto err;
	}
	ret = sync_transfer(udev, EP_IN, &msg, sizeof(msg));
	if (ret < 0) {
		fp_err("sync_transfer EP_IN failed");
		goto err;
	}
	if (msg_header_check(&msg)) {
		fp_err("msg_header_check failed");
		goto err;
	}
	if (msg.cmd != CMD_OK) {
		fp_warn("CMD_OK failed");
		goto err;
	}

	va_start(ap, n_args);
	for (i = 0; i < n_args / 2; i++) {
		uint8_t *val;
		va_arg(ap, int);
		val = va_arg(ap, uint8_t*);
		*val = msg.sige_readreg.regs[i];
	}
	va_end(ap);

	return 0;
err:
	return -1;
}

/*
 * Write synchronously a register from the sensor.
 * Variadic arguments are: int reg, int val, ...
 */
static int dev_set_regs(libusb_device_handle *udev, int n_args, ...)
{
	va_list ap;
	struct egis_msg msg;
	int ret, i;

	if (n_args == 0 || n_args % 2 != 0 || n_args > REG_MAX * 2) {
		fp_err("wrong number of arguments (%d)", n_args);
		goto err;
	}

	msg_header_prepare(&msg);
	msg.cmd = CMD_WRITE_REG;
	msg.egis_writereg.nb = n_args / 2;

	va_start(ap, n_args);
	for (i = 0; i < n_args / 2; i++) {
		msg.egis_writereg.regs[i].reg = va_arg(ap, int);
		msg.egis_writereg.regs[i].val = va_arg(ap, int);
	}
	va_end(ap);

	ret = sync_transfer(udev, EP_OUT, &msg, MSG_HDR_SIZE + 1 + n_args);
	if (ret < 0) {
		fp_err("sync_transfer EP_OUT failed");
		goto err;
	}
	ret = sync_transfer(udev, EP_IN, &msg, sizeof(msg));
	if (ret < 0) {
		fp_err("sync_transfer EP_IN failed");
		goto err;
	}
	if (msg_header_check(&msg)) {
		fp_err("msg_header_check failed");
		goto err;
	}
	if (msg.cmd != CMD_OK) {
		fp_warn("CMD_OK failed");
		goto err;
	}
	return 0;
err:
	return -1;
}

/*
 * Check the model of the sensor.
 */
static int check_info(struct etes603_dev *dev)
{
	uint8_t reg_70, reg_71, reg_72, reg_73;

	if (dev_get_regs(dev->udev, 8, REG_INFO0, &reg_70, REG_INFO1, &reg_71,
		     REG_INFO2, &reg_72, REG_INFO3, &reg_73)) {
		fp_err("cannot read device registers.");
		goto err;
	}

	/* Check device */
	if (reg_70 != 0x4A || reg_71 != 0x44 || reg_72 != 0x49
	    || reg_73 != 0x31) {
		fp_err("unknown device parameters (REG_70:%02X REG_71:%02X "
		       "REG_FIRMWARE:%02X REG_VERSION:%02X)",
		       reg_70, reg_71, reg_72, reg_73);
		/* TODO Don't make it fails the time found all compatible
		 * devices. */
		/* goto err; */
	}
	return 0;
err:
	return -1;
}

/*
 * Ask command 0x20 to the sensor.
 */
static int get_cmd20(struct etes603_dev *dev)
{
	struct egis_msg msg;
	int ret;

	msg_header_prepare(&msg);
	msg.cmd = CMD_20;

	ret = sync_transfer(dev->udev, EP_OUT, &msg, MSG_HDR_SIZE);
	if (ret < 0) {
		fp_err("sync_transfer EP_OUT failed");
		goto err;
	}
	ret = sync_transfer(dev->udev, EP_IN, &msg, sizeof(msg));
	if (ret < 0) {
		fp_err("sync_transfer EP_IN failed");
		goto err;
	}
	if (msg_header_check(&msg)) {
		fp_err("msg_header_check failed");
		goto err;
	}
	/* status or flashtype/flashinfo or ? */
	if (msg.cmd != 0x05
	    || msg.sige_misc.val[0] != 0x00
	    || msg.sige_misc.val[1] != 0x00) {
		fp_warn("unexpected answer CMD_20 from device(%02X %02X %02X)",
			msg.cmd, msg.sige_misc.val[0], msg.sige_misc.val[1]);
	}

	return 0;
err:
	return -1;
}

/*
 * Ask command 0x25 to the sensor.
 */
static int get_cmd25(struct etes603_dev *dev)
{
	struct egis_msg msg;
	int ret;

	msg_header_prepare(&msg);
	msg.cmd = CMD_25;

	ret = sync_transfer(dev->udev, EP_OUT, &msg, MSG_HDR_SIZE);
	if (ret < 0) {
		fp_err("sync_transfer EP_OUT failed");
		goto err;
	}
	ret = sync_transfer(dev->udev, EP_IN, &msg, sizeof(msg));
	if (ret < 0) {
		fp_err("sync_transfer EP_IN failed");
		goto err;
	}
	if (msg_header_check(&msg)) {
		fp_err("msg_header_check failed");
		goto err;
	}
	if (msg.cmd != CMD_OK) {
		fp_err("CMD_OK failed");
		goto err;
	}
	/* flashtype or status or ? */
	if (msg.sige_misc.val[0] != 0x00) {
		fp_warn("unexpected answer for CMD_25 (%02X)",
			msg.sige_misc.val[0]);
	}
	return 0;
err:
	return -1;
}

/*
 * Ask command 0x60 to the sensor.
 */
__attribute__((used))
static int get_cmd60(struct etes603_dev *dev, uint8_t cmd, uint8_t val,
	uint8_t *reg)
{
	struct egis_msg msg;
	int ret;

	msg_header_prepare(&msg);
	msg.cmd = CMD_60;
	/* cmd 0x01: read / cmd 0x02: write */
	msg.sige_misc.val[0] = cmd;
	msg.sige_misc.val[1] = val;

	ret = sync_transfer(dev->udev, EP_OUT, &msg, MSG_HDR_SIZE + cmd - 1);
	if (ret < 0) {
		fp_err("sync_transfer EP_OUT failed");
		goto err_io;
	}
	ret = sync_transfer(dev->udev, EP_IN, &msg, sizeof(msg));
	if (ret < 0) {
		fp_err("sync_transfer EP_IN failed");
		goto err_io;
	}
	if (msg_header_check(&msg)) {
		fp_err("msg_header_check failed");
		goto err_cmd;
	}
	if (msg.cmd != CMD_OK) {
		fp_err("CMD_OK failed");
		goto err_cmd;
	}
	/* Read cmd, so set read value to reg */
	if (cmd == 1 && reg != NULL) {
		*reg = msg.sige_misc.val[0];
	}
	return 0;
err_io:
	return -1;
err_cmd:
	return -2;
}

/*
 * Change the mode of the sensor.
 */
static int set_mode_control(struct etes603_dev *dev, uint8_t mode)
{
	if (dev_set_regs(dev->udev, 2, REG_MODE_CONTROL, mode))
		return -1;
	return 0;
}

/*
 * Initialize the sensor by setting some registers.
 */
static int init(struct etes603_dev *dev)
{
	if (set_mode_control(dev, REG_MODE_SLEEP))
		return -1;
	if (dev_set_regs(dev->udev, 2, REG_50, 0x0F))
		return -2;
	if (dev_set_regs(dev->udev, 2, REG_GAIN, 0x04))
		return -3;
	if (dev_set_regs(dev->udev, 2, REG_VRT, 0x08))
		return -4;
	if (dev_set_regs(dev->udev, 2, REG_VRB, 0x0D))
		return -5;
	if (dev_set_regs(dev->udev, 2, REG_VCO_CONTROL, REG_VCO_RT))
		return -6;
	if (dev_set_regs(dev->udev, 2, REG_DCOFFSET, 0x36))
		return -7;
	if (dev_set_regs(dev->udev, 2, REG_F0, 0x00))
		return -8;
	if (dev_set_regs(dev->udev, 2, REG_F2, 0x00))
		return -9;
	return 0;
}

/*
 * This function sets encryption registers to no encryption.
 */
static int init_enc(struct etes603_dev *dev)
{
	/* Initialize encryption. */
	/* Set registers from 0x41 to 0x48 (0x8 regs) */
	if (dev_set_regs(dev->udev, 16, REG_ENC1, 0x12, REG_ENC2, 0x34,
		     REG_ENC3, 0x56, REG_ENC4, 0x78, REG_ENC5, 0x90,
		     REG_ENC6, 0xAB, REG_ENC7, 0xCD, REG_ENC8, 0xEF)) {
		fp_err("Failed");
		return -1;
	}
	return 0;
}

/*
 * This function set registers 0x20 to 0x37 to default values.
 */
static int init_regs(struct etes603_dev *dev)
{
	/* Set register from 0x20 to 0x37 (0x18 regs) */
	if (dev_set_regs(dev->udev, 48,
		     REG_20, 0x00, REG_21, 0x23, REG_22, 0x21, REG_23, 0x20,
		     REG_24, 0x14, REG_25, 0x6A, REG_26, 0x00, REG_27, 0x00,
		     REG_28, 0x00, REG_29, 0xC0, REG_2A, 0x50, REG_2B, 0x50,
		     REG_2C, 0x4D, REG_2D, 0x03, REG_2E, 0x06, REG_2F, 0x06,
		     REG_30, 0x10, REG_31, 0x02, REG_32, 0x14, REG_33, 0x34,
		     REG_34, 0x01, REG_35, 0x08, REG_36, 0x03, REG_37, 0x21)) {
		fp_err("Failed");
		return -1;
	}
	return 0;
}

/*
 * This function tunes the DCoffset value and adjusts the gain value if
 * required.
 */
static int tune_dc(struct etes603_dev *dev)
{
	uint8_t buf[FRAME_SIZE];
	uint8_t min, max;
	uint8_t dcoffset, gain;

	fp_dbg("Tuning DCoffset");
	/* TODO To get better results, tuning must be done 3 times as in
	 * captured traffic to make sure that the value is correct. */
	/* The default gain should work but it may reach a DCOffset limit so in this
	 * case we decrease the gain. */
	for (gain = GAIN_SMALL_INIT; ; gain--) {
		min = DCOFFSET_MIN;
		max = DCOFFSET_MAX;
		/* Dichotomic search to find at which value the frame becomes
		 * almost black. */
		while (min + 1 < max) {
			dcoffset = (max + min) / 2;
			fp_dbg("Testing DCoffset=0x%02X Gain=0x%02X", dcoffset, gain);
			if (dev_set_regs(dev->udev, 2, REG_DCOFFSET, dcoffset))
				goto err_tunedc;
			/* vrt:0x15 vrb:0x10 are constant in all tuning frames. */
			if (dev_get_frame(dev->udev, FRAME_WIDTH, 0x01, gain, 0x15, 0x10, buf))
				goto err_tunedc;
			if (process_frame_empty(buf, FRAME_SIZE, 0))
				max = dcoffset;
			else
				min = dcoffset;
		}
		if (max < DCOFFSET_MAX) {
			dcoffset = max + 1;
			break;
		}
	}
	fp_dbg("-> DCoffset=0x%02X Gain=0x%02X", dcoffset, gain);
	dev->gain = gain;
	dev->dcoffset = dcoffset;

	/* ??? how reg21 / reg22 are calculated */
	if (dev_set_regs(dev->udev, 4, REG_21, 0x23, REG_22, 0x21))
		goto err_write;
	if (dev_set_regs(dev->udev, 2, REG_GAIN, gain))
		goto err_write;
	if (dev_set_regs(dev->udev, 2, REG_DCOFFSET, dcoffset))
		goto err_write;
	/* In captured traffic, read REG_GAIN, REG_VRT, and REG_VRB registers. */

	return 0;
err_tunedc:
	fp_err("Error tuning DC/gain parameter");
	return -1;
err_write:
	fp_err("Error setting registers");
	return -2;
}

/*
 * This function tunes the value for DTVRT and adjusts DCOFFSET if needed.
 */
static int tune_dtvrt(struct etes603_dev *dev)
{
	uint8_t reg_e5;
	uint8_t reg_50, reg_51;
	uint8_t reg_59, reg_5a, reg_5b;
	uint8_t dtvrt;
	uint8_t dcoffset_ct;

	assert(dev->dcoffset);
	/* Use DCOffset for frame capture as default. */
	dcoffset_ct = dev->dcoffset;

	/* Save registers to reset it at the end. */
	if (dev_get_regs(dev->udev, 2, REG_VCO_CONTROL, &reg_e5))
		goto err_rw;
	if (dev_get_regs(dev->udev, 4, REG_50, &reg_50, REG_51, &reg_51))
		goto err_rw;
	if (dev_get_regs(dev->udev, 6, REG_59, &reg_59, REG_5A, &reg_5a, REG_5B, &reg_5b))
		goto err_rw;

restart:
	if (set_mode_control(dev, REG_MODE_SLEEP))
		goto err_rw;
	if (dev_set_regs(dev->udev, 2, REG_DCOFFSET, dcoffset_ct))
		goto err_rw;
	if (dev_set_regs(dev->udev, 2, REG_VCO_CONTROL, REG_VCO_IDLE))
		goto err_rw;
	if (dev_set_regs(dev->udev, 2, REG_50, reg_50 | 0x80))
		goto err_rw;
	if (dev_set_regs(dev->udev, 2, REG_51, reg_51 & 0xF7))
		goto err_rw;
	if (dev_set_regs(dev->udev, 6, REG_59, 0x18, REG_5A, 0x08, REG_5B, 0x00))
		goto err_rw;

	if (set_mode_control(dev, REG_MODE_CONTACT))
		goto err_rw;

	fp_dbg("Tuning of DTVRT");
	dtvrt = DTVRT_MAX;
	if (dev_set_regs(dev->udev, 2, REG_DTVRT, dtvrt))
		goto err_tune;
	/* Arbitrary lowest value for DCOffset. */
	while (!contact_detect(dev) && dcoffset_ct > 0x10) {
		if (dtvrt > 5) {
			dtvrt -= 5;
		} else {
			/* Decrease DCoffset if cannot adjust a value for
			 * DTVRT. */
			dcoffset_ct--;
			fp_dbg("Decrease DCoffset=0x%02X for contact "
				"detection (DTVRT)", dcoffset_ct);
			goto restart;
		}
		fp_dbg("Testing DTVRT=0x%02X DCoffset=0x%02X", dtvrt, dcoffset_ct);
		if (dev_set_regs(dev->udev, 2, REG_DTVRT, dtvrt))
			goto err_tune;
	}
	dtvrt += 5;
	fp_dbg("-> DTVRT=0x%02X DCoffset=0x%02X", dtvrt, dcoffset_ct);
	dev->dtvrt = dtvrt;
	dev->dcoffset_ct = dcoffset_ct;

	if (set_mode_control(dev, REG_MODE_SLEEP))
		goto err_rw;
	/* Reset registers value from initial values. */
	if (dev_set_regs(dev->udev, 2, REG_VCO_CONTROL, reg_e5))
		goto err_rw;
	if (dev_set_regs(dev->udev, 4, REG_50, reg_50, REG_51, reg_51))
		goto err_rw;
	if (dev_set_regs(dev->udev, 6, REG_59, reg_59, REG_5A, reg_5a, REG_5B, reg_5b))
		goto err_rw;
	/* Reset DCOffset for frame capturing. */
	if (dev_set_regs(dev->udev, 2, REG_DCOFFSET, dev->dcoffset))
		goto err_rw;
	/* Set value found for DTVRT. */
	if (dev_set_regs(dev->udev, 2, REG_DTVRT, dtvrt))
		goto err_rw;

	return 0;
err_rw:
	fp_err("cannot read/write registers");
	return -1;
err_tune:
	fp_err("error tuning DTVRT/DCoffset (cannot write registers)");
	return -2;
}

/*
 * Tune value of VRT and VRB for contrast and brightness.
 */
static int tune_vrb(struct etes603_dev *dev)
{
	static const unsigned int BW = 0x08; /* Border width*/
	static const unsigned int FBW = FRAME_WIDTH / 2; /* Frame byte width */
	uint8_t buf[FRAME_SIZE];
	/* VRT(reg E1)=0x0A and VRB(reg E2)=0x10 are starting values */
	/* reg_e0 = 0x23 is sensor normal/small gain */
	uint8_t reg_gain, reg_vrt = 0x0A, reg_vrb = 0x10, reg_dc;
	unsigned int i, j;
	double hist[16];
	double white_mean, black_mean;

	fp_dbg("Tuning of VRT/VRB");

	if (dev_get_regs(dev->udev, 2, REG_GAIN, &reg_gain))
		goto err;

	/* Reduce DCoffset by 1 to allow tuning */
	if (dev_get_regs(dev->udev, 2, REG_DCOFFSET, &reg_dc))
		goto err;
	if (dev_set_regs(dev->udev, 2, REG_DCOFFSET, reg_dc - 1))
		goto err;

	while (reg_vrt < VRT_MAX && reg_vrb < VRB_MAX) {
		fp_dbg("Testing VRT=0x%02X VRB=0x%02X", reg_vrt, reg_vrb);
		/* Clean histogram */
		for (i = 0; i < 16; i++)
			hist[i] = 0.0;
		if (dev_get_frame(dev->udev, FRAME_WIDTH, 0x01, reg_gain,
				  reg_vrt, reg_vrb, buf))
			goto err;
		/* fill up histogram using 4 rows of the frame */
		for (j = 0; j < FRAME_HEIGHT; j++) {
			/* only center pixels (0x50 pixels) */
			for (i = BW + j * FBW; i < FBW - BW + j * FBW; i++)
				hist[buf[i] >> 4]++;
		}
		/* histogram average */
		for (i = 0; i < 16; i++) {
			hist[i] = hist[i] / ((FBW - BW * 2) * FRAME_HEIGHT);
		}
		/* Average black/white pixels (full black and full white pixels
		 * are excluded). */
		black_mean = white_mean = 0.0;
		for (i = 1; i < 8; i++)
			black_mean += hist[i];
		for (i = 8; i < 15; i++)
			white_mean += hist[i];
		fp_dbg("fullb=%6f black=%6f grey=%6f white=%6f fullw=%6f",
			hist[0], black_mean, black_mean+white_mean, white_mean,
			hist[15]);

		/* Tuning VRT/VRB -> contrast and brightness */
		if (hist[0] + black_mean > 0.95) {
			fp_dbg("Image is too dark, reducing DCoffset");
			reg_dc--;
			dev_set_regs(dev->udev, 2, REG_DCOFFSET, reg_dc-1);
			//break;
		}
		if (hist[15] > 0.95) {
			fp_dbg("Image is too bright, trying increase DCoffset");
			reg_dc++;
			dev_set_regs(dev->udev, 2, REG_DCOFFSET, reg_dc-1);
			//break;
		}
		if ((black_mean > 0.1) && (white_mean > 0.1) && (black_mean + white_mean > 0.4)) {
			/* The image seems balanced. */
			break;
		}
		if (reg_vrt >= 2 * reg_vrb - 0x0a) {
			reg_vrt++; reg_vrb++;
		} else {
			reg_vrt++;
		}
		/* Check maximum for vrt/vrb */
		/* TODO if maximum is reached, leave with an error? */
		if (reg_vrt > VRT_MAX)
			reg_vrt = VRT_MAX;
		if (reg_vrb > VRB_MAX)
			reg_vrb = VRB_MAX;

	}
	fp_dbg("-> VRT=0x%02X VRB=0x%02X", reg_vrt, reg_vrb);
	dev->vrt = reg_vrt;
	dev->vrb = reg_vrb;

	/* Reset the DCOffset */
	if (dev_set_regs(dev->udev, 2, REG_DCOFFSET, reg_dc))
		goto err;
	/* In traces, REG_26/REG_27 are set. purpose? values? */
	if (dev_set_regs(dev->udev, 4, REG_26, 0x11, REG_27, 0x00))
		goto err;
	/* Set Gain/VRT/VRB values found */
	if (dev_set_regs(dev->udev, 6, REG_GAIN, reg_gain, REG_VRT, reg_vrt, REG_VRB, reg_vrb))
		goto err;
	/* In traces, Gain/VRT/VRB are read again. */

	return 0;
err:
	fp_err("cannot read/write registers");
	return -1;
}

/*
 * Retrieve a frame (synchronous function).
 */
static int frame_capture(struct etes603_dev *dev, uint8_t *buf)
{
	/* Note that the length parameter can be changed but 0xC0 is the best value */
	return dev_get_frame(dev->udev, FRAME_WIDTH, 0x00, 0x00, 0x00, 0x00, buf);
}

/*
 * Prepare the sensor to capture a frame.
 */
static int frame_prepare_capture(struct etes603_dev *dev)
{
	assert(dev->dcoffset && dev->gain && dev->vrt && dev->vrb);
	if (set_mode_control(dev, REG_MODE_SLEEP))
		return -1;
	/* Set tuned realtime configuration. */
	if (dev_set_regs(dev->udev, 2, REG_DCOFFSET, dev->dcoffset))
		return -2;
	if (dev_set_regs(dev->udev, 6, REG_GAIN, dev->gain, REG_VRT, dev->vrt, REG_VRB, dev->vrb))
		return -3;
	/* Set the sensor to realtime capturing (0x14). */
	if (dev_set_regs(dev->udev, 2, REG_VCO_CONTROL, REG_VCO_RT))
		return -4;
	/* REG_04 is frame configuration */
	if (dev_set_regs(dev->udev, 2, REG_04, 0x00))
		return -5;
	if (set_mode_control(dev, REG_MODE_SENSOR))
		return -6;
	return 0;
}

/*
 * Prepare the sensor to capture a fingerprint image.
 */
static int fp_configure(struct etes603_dev *dev)
{
	/* REG_10 is required to get a good fingerprint frame (exact meaning?) */
	return dev_set_regs(dev->udev, 2, REG_10, 0x92);
}


/*
 * This function opens the sensor and initialize it.
 * Returns NULL on error.
 */
static struct etes603_dev *sensor_open(libusb_device_handle *udev)
{
	int ret;
	struct etes603_dev *dev;

	if ((dev = malloc(sizeof(struct etes603_dev))) == NULL) {
		fp_err("cannot allocate memory");
		return NULL;
	}

	dev->udev = udev;
	if ((dev->braw = malloc(FRAME_SIZE * 1000)) == NULL) {
		fp_err("cannot allocate memory");
		goto err_free_dev;
	}
	dev->braw_end = dev->braw + (FRAME_SIZE * 1000);
	dev->braw_cur = dev->braw;

	if ((ret = check_info(dev)) != 0) {
		fp_err("check_info failed (err=%d)", ret);
		goto err_free_buffer;
	}
	if ((ret = get_cmd20(dev)) != 0) {
		fp_err("get_cmd20 failed (err=%d)", ret);
		goto err_free_buffer;
	}
	if ((ret = get_cmd25(dev)) != 0) {
		fp_err("get_cmd25 failed (err=%d)", ret);
		goto err_free_buffer;
	}
	if ((ret = init(dev)) != 0) {
		fp_err("init failed (err=%d)", ret);
		goto err_free_buffer;
	}
	if ((ret = init_enc(dev)) != 0) {
		fp_err("init_enc failed (err=%d)", ret);
		goto err_free_buffer;
	}
	if ((ret = init_regs(dev)) != 0) {
		fp_err("init_regs failed (err=%d)", ret);
		goto err_free_buffer;
	}
	if ((ret = tune_dc(dev)) != 0) {
		fp_err("tune_dc failed (err=%d)", ret);
		goto err_free_buffer;
	}
	if ((ret = tune_dtvrt(dev)) != 0) {
		fp_err("tune_dtvrt failed (err=%d)", ret);
		goto err_free_buffer;
	}
	if ((ret = tune_vrb(dev)) != 0) {
		fp_err("tune_vrb failed (err=%d)", ret);
		goto err_free_buffer;
	}
	/* Configure fingerprint frame (set register value for this session) */
	if ((ret = fp_configure(dev)) != 0) {
		fp_err("fp_configure failed (err=%d)", ret);
		goto err_free_buffer;
	}

	return dev;

err_free_buffer:
	free(dev->braw);
err_free_dev:
	free(dev);
	return NULL;
}


/*
 * Detect the contact by reading register 0x03.
 */
static int contact_detect(struct etes603_dev *dev)
{
	uint8_t reg_03;
	if (dev_get_regs(dev->udev, 2, REG_03, &reg_03))
		return -1;
	/* 83,A3:no 93,B3:yes */
	return (reg_03 >> 4) & 0x1;
}

/*
 * Initialize the finger contact sensor.
 * DTVRT tuning must be done before and it may modify DCOffset register.
 */
static int contact_polling_init(struct etes603_dev *dev)
{
	uint8_t reg_50;
	assert(dev->dcoffset_ct);
	/* ? Check if always same values */
	if (set_mode_control(dev, REG_MODE_SLEEP)
	    || dev_set_regs(dev->udev, 2, REG_VCO_CONTROL, REG_VCO_IDLE)
	    || dev_set_regs(dev->udev, 2, REG_59, 0x18)
	    || dev_set_regs(dev->udev, 2, REG_5A, 0x08)
	    || dev_set_regs(dev->udev, 2, REG_5B, 0x10))
		return -1;
	if (set_mode_control(dev, REG_MODE_CONTACT)
	    || dev_get_regs(dev->udev, 2, REG_50, &reg_50)
	    || dev_set_regs(dev->udev, 2, REG_50, ((reg_50 & 0x7F) | 0x80)))
		return -2;
	if (dev_set_regs(dev->udev, 2, REG_DCOFFSET, dev->dcoffset_ct))
		return -3;
	return 0;
}

/*
 * Change the sensor mode after contact detection.
 */
static int contact_polling_exit(struct etes603_dev *dev)
{
	/* Set VCO_CONTROL back to realtime mode */
	if (set_mode_control(dev, REG_MODE_SLEEP)
	    || dev_set_regs(dev->udev, 2, REG_VCO_CONTROL, REG_VCO_RT))
		return -1;
	return 0;
}

/*
 * Detect finger contact with the sensor.
 * Returns 1 if contact detected or 0 if no contact and the timeout is expired.
 * Returns < 0 when an error happens.
 */
static int contact_polling(struct etes603_dev *dev)
{
	/* Contact polling power consumption is lower than capturing a lot of
	 * frame. From website: Typical 15 mA @ USB2.0 imaging/navigating and
	 * Typical <500uA finger detect mode. */
	struct timeval endtime;
	struct timeval curtime = {.tv_sec = CS_DETECT_TIMEOUT / 1000,
				  .tv_usec = 0};
	int contact;

	if (contact_polling_init(dev)) {
		fp_err("cannot initialize polling.");
		contact = -1;
		goto end;
	}

	if (gettimeofday(&endtime, NULL)) {
		fp_err("gettimeofday failed with %d", errno);
		contact = -2;
		goto end;
	}
	timeradd(&endtime, &curtime, &endtime);

	do {
		contact = contact_detect(dev);
		if (contact == 1) {
			goto end;
		}

		/* 5 ms between each detect */
		usleep(CS_DETECT_DELAY * 1000);

		if (gettimeofday(&curtime, NULL)) {
			fp_err("gettimeofday failed with %d", errno);
			contact = -2;
			goto end;
		}
	} while (timercmp(&curtime, &endtime, <));

end:
	contact_polling_exit(dev);

	return contact;
}

/*
 * Ask the sensor for a fingerprint frame.
 */
__attribute__((used))
static int fp_capture(struct etes603_dev *dev, uint8_t *buf, size_t bufsize)
{
	if (bufsize < FRAMEFP_SIZE)
		return -1;

	if (fp_configure(dev))
		return -2;

	if (set_mode_control(dev, REG_MODE_FP))
		return -3;

	if (dev_get_fp(dev->udev, buf))
		return -4;

	if (set_mode_control(dev, REG_MODE_SLEEP))
		return -5;

	/* Set VCO_CONTROL to realtime mode (maybe not needed) */
	if (dev_set_regs(dev->udev, 2, REG_VCO_CONTROL, REG_VCO_RT))
		return -6;

	return 0;
}

/*
 * Do a full finger capture using frames and reconstruction.
 * This function uses synchronous tranfers.
 */
__attribute__((used))
static int fp_capture_asm(struct etes603_dev *dev, uint8_t *braw, size_t bsize)
{
	uint8_t bframe[FRAME_SIZE];
	uint8_t *braw_cur = braw;
	uint8_t *braw_end = braw + bsize;

	if (frame_prepare_capture(dev))
		goto err;
	if (frame_capture(dev, bframe))
		goto err;
	memcpy(braw, bframe, FRAME_SIZE);

	/* Wait a non-empty frame. */
	do {
		if (frame_capture(dev, bframe))
			goto err;
	} while (process_frame_empty(bframe, FRAME_SIZE, 1));

	/* While is not aborted and the frame is not empty and that the buffer
	 * is not full, retrieve and process frame. */
	do {
		braw_cur = process_frame(braw_cur, bframe);
		if (frame_capture(dev, bframe))
			goto err;
	} while (!process_frame_empty(bframe, FRAME_SIZE, 1)
		 && (braw_cur + FRAME_SIZE) < braw_end);

	/* Set the sensor in sleep mode (needed?) */
	if (set_mode_control(dev, REG_MODE_SLEEP))
		goto err;

	return 0;
err:
	fp_err("Error communicating with the device");
	return -1;
}


/*
 * Detect when a finger is on the sensor.
 */
__attribute__((used))
static int sensor_detect_finger(struct etes603_dev *dev)
{
	int ret;
	/* There are different ways to detect fingers. We can capture frames
	 * until a non-empty frame is found or we can use the contact sensor as
	 * done here.
	 */
	ret = contact_polling(dev);
	return ret;
}

/*
 * Close the sensor by setting some registers.
 */
static int sensor_close(struct etes603_dev *dev, libusb_device_handle *udev)
{
	if (udev) {
		/* Values from a captured frame */
		dev_set_regs(udev, 24, REG_DCOFFSET, 0x31, REG_GAIN, 0x23,
			REG_DTVRT, 0x0D, REG_51, 0x30, REG_VCO_CONTROL, REG_VCO_IDLE, REG_F0, 0x01,
			REG_F2, 0x4E, REG_50, 0x8F, REG_59, 0x18, REG_5A, 0x08, REG_5B, 0x10,
			REG_MODE_CONTROL, REG_MODE_CONTACT);
	}

	if (dev) {
		free(dev->braw);
		free(dev);
	}

	return 0;
}


/* Processing functions */

/*
 * Return the brightness of a frame
 */
static unsigned int process_get_brightness(uint8_t *f, size_t s)
{
	unsigned int i, sum = 0;
	for (i = 0; i < s; i++) {
		sum += f[i] & 0x0F;
		sum += f[i] >> 4;
	}
	return sum;
}

/*
 * Return true if the frame is almost empty.
 * If mode is 0, it is high sensibility for device tuning.
 * Otherwise, for capture mode.
 */
static int process_frame_empty(uint8_t *frame, size_t size, int mode)
{
	unsigned int sum = process_get_brightness(frame, size);
	/* Allow an average of 'threshold' luminosity per pixel */
	if (mode) {
		/* mode capture */
		if (sum < size * 2)
			return 1;
	} else {
		/* mode tuning */
		if (sum < size)
			return 1;
	}
	return 0;
}

/*
 * Compare lines in 'dst' and 'src'.
 * Return the number of new lines in 'src'.
 */
static int process_find_dup(uint8_t *dst, uint8_t *src, uint8_t width,
	uint8_t height)
{
	int v;
	unsigned int i, j;
	/* Total errors when substract src from dst */
	unsigned int sum_error;
	/* Number of lines that matches */
	unsigned int nb = height;
	/* Size in byte is width * height / 2 pixels per byte */
	unsigned int bwidth = width / 2;
	unsigned int bsize = bwidth * height;
	/* Maximal error threshold to consider that lines match. */
	/* Value 6 is empirical. */
	unsigned int max_error = process_get_brightness(src, bsize) / 6;
	/* Typical frame: 384 bytes / 196 px width / 4 bits value */
	/* Scan lines, assuming first that all lines match. */
	for (i = 0; i < height; i++) {
		sum_error = 0;
		for (j = 0; j < bwidth * (height - i); j++) {
			/* subtract and absolute value */
			v = (int)(dst[j] & 0x0F) - (int)(src[j] & 0x0F);
			/* note: we could use ^2 */
			v = v >= 0 ? v : -v;
			sum_error += v;
			v = (int)(dst[j] >> 4) - (int)(src[j] >> 4);
			v = v >= 0 ? v : -v;
			sum_error += v;
		}
		/* The usage of int makes value imprecise when divide. */
		sum_error *= 127;
		sum_error = sum_error / j; /* Avg error/pixel */
		if (sum_error < max_error) {
			max_error = sum_error;
			nb = i;
		}
		dst += bwidth; /* Next line */
	}
	return nb;
}

/*
 * The first 'merge' bytes from src and dst are merged then raw copy.
 */
static void merge_and_append(uint8_t *dst, uint8_t *src, size_t merge,
	size_t size)
{
	size_t i;
	uint8_t pxl, pxh;
	assert(merge <= size);
	for (i = 0; i < merge; i++) {
		pxl = ((dst[i] & 0x0F) + (src[i] & 0x0F)) >> 1;
		pxh = ((dst[i] >> 4) + (src[i] >> 4)) >> 1;
		dst[i] = pxl | (pxh << 4);
	}
	for (i = merge; i < size; i++) {
		dst[i] = src[i];
	}
}



/*
 * Integrate the new frame 'src' into previous assembled frames 'dst'.
 * 'dst' must point to the last buffer and have 384 bytes free at the end to append data.
 * 'src' must point to the frame received (384 bytes).
 */
static uint8_t *process_frame(uint8_t *dst, uint8_t *src)
{
	int new_line;

	/* TODO sweep direction to determine... merging will be different. */
	new_line = process_find_dup(dst, src, FRAME_WIDTH, FRAME_HEIGHT);
	dst += (FRAME_WIDTH / 2) * new_line;
	/* merge_and_append give a better result than just copying */
	merge_and_append(dst, src, (FRAME_HEIGHT - new_line) * (FRAME_WIDTH / 2), FRAME_SIZE);
	/* memcpy(dst, src, FRAME_SIZE); */

	return dst;
}

/* Transform 4 bits image to 8 bits image */
static void process_transform4_to_8(uint8_t *input, unsigned int input_size,
	uint8_t *output)
{
	unsigned int i, j = 0;
	for (i = 0; i < input_size; i++, j += 2) {
		/* 16 gray levels transform to 256 levels using << 4 */
		output[j] = input[i] & 0xF0;
		output[j+1] = input[i] << 4;
	}
}


/* libfprint stuff */

/*
 * Called when the deactivation was requested.
 */
static void complete_deactivation(struct fp_img_dev *idev)
{
	struct etes603_dev *dev = idev->priv;

	/* TODO I guess I should not completely deinit the sensor? */
	/* TODO Change at least to SLEEP_MODE ? */
	dev->deactivating = FALSE;
	fpi_imgdev_deactivate_complete(idev);
}

/*
 * Transforms the raw data to fprint data and submit it to fprint.
 */
static int transform_to_fpi(struct fp_img_dev *idev)
{
	struct fp_img *img;
	struct etes603_dev *dev = idev->priv;

	if (dev->mode == 1) {
		/* Assembled frames */
		/* braw_cur points to the last frame so needs to adjust to end */
		unsigned int size = dev->braw_cur + FRAME_SIZE - dev->braw;
		/* es603 has 2 pixels per byte. */
		img = fpi_img_new(size * 2);
		img->flags = FP_IMG_COLORS_INVERTED | FP_IMG_V_FLIPPED;
		img->height = size * 2 / FRAME_WIDTH;
		/* img->width could be set 256 always but need a new function to handle this */
		img->width = FRAME_WIDTH;
		process_transform4_to_8(dev->braw, size, (uint8_t*)img->data);
	} else {
		/* FingerPrint Frame */
		img = fpi_img_new(FRAMEFP_SIZE * 2);
		/* Images received are white on black, so invert it (FP_IMG_COLORS_INVERTED) */
		/* TODO for different sweep direction ? FP_IMG_V_FLIPPED | FP_IMG_H_FLIPPED */
		img->flags = FP_IMG_COLORS_INVERTED | FP_IMG_V_FLIPPED;
		/* img->width can only be changed when -1 was set at init */
		img->width = FRAMEFP_WIDTH;
		img->height = FRAMEFP_HEIGHT;
		process_transform4_to_8(dev->braw, FRAMEFP_SIZE, (uint8_t*)img->data);
	}
	/* Send image to fpi */
	fpi_imgdev_image_captured(idev, img);
	/* Indicate that the finger is removed. */
	fpi_imgdev_report_finger_status(idev, FALSE);

	return 0;
}

/* state for asynchronous functions */
#define STATE_INIT                     1
#define STATE_FINGER_REQ_SEND          2
#define STATE_FINGER_REQ_RECV          3
#define STATE_FINGER_ANS               4
#define STATE_CAPTURING_REQ_SEND       5
#define STATE_CAPTURING_REQ_RECV       6
#define STATE_CAPTURING_ANS            7
#define STATE_INIT_FP_REQ_SEND         8
#define STATE_INIT_FP_REQ_RECV         9
#define STATE_INIT_FP_ANS              10
#define STATE_CAPTURING_FP_REQ_SEND    11
#define STATE_CAPTURING_FP_REQ_RECV    12
#define STATE_CAPTURING_FP_ANS         13
#define STATE_DEACTIVATING             14

static int async_transfer(struct fp_img_dev *dev, unsigned char ep,
		unsigned char *msg_data, unsigned int msg_size);

/*
 * Asynchronous function callback for asynchronous read buffer.
 */
static void async_transfer_cb(struct libusb_transfer *transfer)
{
	struct fp_img_dev *idev = transfer->user_data;
	struct etes603_dev *pdata = idev->priv;
	struct egis_msg *msg;

	/* Check status except if initial state (entrypoint) */
	if (pdata->state != STATE_INIT
	    && transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		fp_warn("transfer is not completed (state=%d/status=%d)",
			pdata->state, transfer->status);
		goto err;
	}
	/* To ensure non-fragmented message, LIBUSB_TRANSFER_SHORT_NOT_OK is
	 * used. */
	if (pdata->state != STATE_INIT) {
		debug_output(transfer->endpoint, transfer->buffer, transfer->actual_length);
	}

goback:

	if (pdata->deactivating) {
		/* libusb_cancel_transfer should not be required here since the
		 * transfer has to be complete. */
		complete_deactivation(idev);
		goto err;
	}

	switch (pdata->state) {
	case STATE_INIT:
		fp_dbg("Waiting finger to be present...");
		pdata->state = STATE_FINGER_REQ_SEND;
		/* no break, we are continuing with next step */

	case STATE_FINGER_REQ_SEND:
		msg = malloc(sizeof(struct egis_msg));
		msg_get_frame(msg, FRAME_WIDTH, 0, 0, 0, 0);
		if (async_transfer(idev, EP_OUT, (unsigned char *)msg, MSG_HDR_SIZE + 6)) {
			goto err;
		}
		pdata->state = STATE_FINGER_REQ_RECV;
		break;

	case STATE_FINGER_REQ_RECV:
		/* The request succeeds. */
		msg = malloc(FRAME_SIZE);
		memset(msg, 0, FRAME_SIZE);
		/* Now ask for receiving data. */
		if (async_transfer(idev, EP_IN, (unsigned char *)msg, FRAME_SIZE)) {
			goto err;
		}
		pdata->state = STATE_FINGER_ANS;
		break;

	case STATE_FINGER_ANS:
		if (process_frame_empty(transfer->buffer, FRAME_SIZE, 1)) {
			/* No finger, request a new frame. */
			pdata->state = STATE_FINGER_REQ_SEND;
			goto goback;
		}
		/* Indicate that the finger is present. */
		fpi_imgdev_report_finger_status(idev, TRUE);
		/* Select mode for capturing. */
		if (pdata->mode == 1) {
			fp_dbg("Finger is detected, assembled frames mode");
			pdata->state = STATE_CAPTURING_REQ_SEND;
		} else {
			fp_dbg("Finger is detected, FP mode");
			pdata->state = STATE_INIT_FP_REQ_SEND;
			goto MODE_FP;
		}
		/* no break, continue to state STATE_CAPTURING_REQ_SEND. */

	case STATE_CAPTURING_REQ_SEND:
		msg = malloc(sizeof(struct egis_msg));
		msg_get_frame(msg, FRAME_WIDTH, 0, 0, 0, 0);
		if (async_transfer(idev, EP_OUT, (unsigned char *)msg, MSG_HDR_SIZE + 6)) {
			goto err;
		}
		pdata->state = STATE_CAPTURING_REQ_RECV;
		break;

	case STATE_CAPTURING_REQ_RECV:
		/* The request succeeds. */
		msg = malloc(FRAME_SIZE);
		memset(msg, 0, FRAME_SIZE);
		/* Receiving data. */
		if (async_transfer(idev, EP_IN, (unsigned char *)msg, FRAME_SIZE)) {
			goto err;
		}
		pdata->state = STATE_CAPTURING_ANS;
		break;

	case STATE_CAPTURING_ANS:
		if (process_frame_empty(transfer->buffer, FRAME_SIZE, 1)) {
			/* Finger leaves, send final image. */
			pdata->state = STATE_DEACTIVATING;
			transform_to_fpi(idev);
			break;
		}
		/* Merge new frame with current image. */
		pdata->braw_cur = process_frame(pdata->braw_cur, transfer->buffer);
		if ((pdata->braw_cur + FRAME_SIZE) >= pdata->braw_end) {
			fp_warn("STATE_CAPTURING_ANS: Buffer is full");
			/* Buffer is full, send final image. */
			pdata->state = STATE_DEACTIVATING;
			transform_to_fpi(idev);
			break;
		}
		/* Ask new frame. */
		pdata->state = STATE_CAPTURING_REQ_SEND;
		goto goback;

	case STATE_INIT_FP_REQ_SEND:
MODE_FP:
		/* Change to mode REG_MODE_FP */
		msg = malloc(sizeof(struct egis_msg));
		msg_header_prepare(msg);
		msg->cmd = CMD_WRITE_REG;
		msg->egis_writereg.nb = 0x01;
		msg->egis_writereg.regs[0].reg = REG_MODE_CONTROL;
		msg->egis_writereg.regs[0].val = REG_MODE_FP;
		if (async_transfer(idev, EP_OUT, (unsigned char *)msg, MSG_HDR_SIZE + 3)) {
			goto err;
		}
		pdata->state = STATE_INIT_FP_REQ_RECV;
		break;

	case STATE_INIT_FP_REQ_RECV:
		/* The request succeeds. */
		msg = malloc(sizeof(struct egis_msg));
		memset(msg, 0, sizeof(struct egis_msg));
		/* Receiving data. */
		if (async_transfer(idev, EP_IN, (unsigned char *)msg, MSG_HDR_SIZE)) {
			goto err;
		}
		pdata->state = STATE_INIT_FP_ANS;
		break;

	case STATE_INIT_FP_ANS:
		msg = (struct egis_msg *)transfer->buffer;
		if (msg_header_check(msg))
			goto err;
		if (msg->cmd != CMD_OK)
			goto err;
		pdata->state = STATE_CAPTURING_FP_REQ_SEND;
		/* continuing, now ask for the fingerprint frame */

	case STATE_CAPTURING_FP_REQ_SEND:
		msg = malloc(sizeof(struct egis_msg));
		msg_get_fp(msg, 0x01, 0xF4, 0x02, 0x01, 0x64);
		if (async_transfer(idev, EP_OUT, (unsigned char *)msg, MSG_HDR_SIZE + 5)) {
			goto err;
		}
		pdata->state = STATE_CAPTURING_FP_REQ_RECV;
		break;

	case STATE_CAPTURING_FP_REQ_RECV:
		/* The request succeeds. */
		msg = malloc(FRAMEFP_SIZE);
		memset(msg, 0, FRAMEFP_SIZE);
		/* Receiving data. */
		if (async_transfer(idev, EP_IN, (unsigned char *)msg, FRAMEFP_SIZE)) {
			goto err;
		}
		pdata->state = STATE_CAPTURING_FP_ANS;
		break;

	case STATE_CAPTURING_FP_ANS:
		memcpy(pdata->braw, transfer->buffer, transfer->actual_length);
		/* Set STATE_DEACTIVATING before sending image because
		 * deactivation is called when image is sent. */
		pdata->state = STATE_DEACTIVATING;
		transform_to_fpi(idev);
		break;

	case STATE_DEACTIVATING:
		fp_dbg("STATE_DEACTIVATING:");
		/* TODO change sensor to sleep mode but not complete deactivation */
		break;

	default:
		fp_err("Umknown state");
		goto err;
	}

	/* No need to free buffer or transfer, LIBUSB_TRANSFER_FREE_BUFFER and
	 * LIBUSB_TRANSFER_FREE_TRANSFER are used. */

	return;
err:
	pdata->state = STATE_DEACTIVATING;
	fp_err("Error occured in async process");
	fpi_imgdev_session_error(idev, -EIO);
	return;
}

/*
 * Asynchronous read buffer transfer.
 */
static int async_transfer(struct fp_img_dev *idev, unsigned char ep,
		unsigned char *msg_data, unsigned int msg_size)
{
	struct libusb_transfer *transfer = libusb_alloc_transfer(0);

	if (!transfer)
		return -ENOMEM;

	libusb_fill_bulk_transfer(transfer, idev->udev, ep, msg_data, msg_size,
			async_transfer_cb, idev, BULK_TIMEOUT);
	transfer->flags = LIBUSB_TRANSFER_SHORT_NOT_OK
			| LIBUSB_TRANSFER_FREE_BUFFER
			| LIBUSB_TRANSFER_FREE_TRANSFER;

	if (libusb_submit_transfer(transfer)) {
		libusb_free_transfer(transfer);
		return -1;
	}
	return 0;
}

/*
 * Libfprint asks for activation.
 */
static int dev_activate(struct fp_img_dev *idev, enum fp_imgdev_state state)
{
	char *mode;
	struct libusb_transfer fake_transfer;
	struct etes603_dev *dev = idev->priv;

	/* TODO See how to manage state */
	if (state != IMGDEV_STATE_INACTIVE) {
		fp_err("The driver is in unknown state: %d.", state);
		/*return -1;*/
	}

	if (dev == NULL || dev->braw == NULL || dev->braw_end < dev->braw) {
		fp_warn("idev->priv is not initialized properly.");
		return -1;
	}

	/* Reset info and data */
	dev->deactivating = FALSE;
	dev->braw_cur = dev->braw;
	memset(dev->braw, 0, (dev->braw_end - dev->braw));
	/* Use default mode (FingerPrint, FP) or use environment defined mode */
	dev->mode = 0;
	if ((mode = getenv("ETES603_MODE")) != NULL) {
		if (mode[0] == '1')
			dev->mode = 1;
	}

	/* Preparing capture */
	frame_prepare_capture(dev);

	/* Enable an entrypoint in the asynchronous mess. */
	dev->state = STATE_INIT;
	fake_transfer.user_data = idev;
	async_transfer_cb(&fake_transfer);

	fpi_imgdev_activate_complete(idev, 0);
	return 0;
}

/*
 * Deactivating device.
 */
static void dev_deactivate(struct fp_img_dev *idev)
{
	struct etes603_dev *dev = idev->priv;
	/* complete_deactivation can be called asynchronously. */
	if (dev->state != STATE_DEACTIVATING)
		dev->deactivating = TRUE;
	else
		complete_deactivation(idev);
}

/*
 * Device initialization.
 */
static int dev_init(struct fp_img_dev *idev, unsigned long driver_data)
{
	int ret;
	struct etes603_dev *dev;

	if (driver_data != 0x0603) {
		fp_err("This driver has been only tested on ES603 device. "
		       "(driver_data=%lu)", driver_data);
		/* In case compatible device needs specific parameters */
		return -1;
	}

	ret = libusb_claim_interface(idev->udev, 0);
	if (ret != LIBUSB_SUCCESS) {
		fp_err("libusb_claim_interface failed on interface 0 "
		       "(err=%d)", ret);
		return ret;
	}

	/* Note: it does make sense to use asynchronous method for initializing
	 * the device. It also simplifies a lot the design of the driver. */
	/* Initialize the sensor */
	if ((dev = sensor_open(idev->udev)) == NULL) {
		fp_err("cannot open sensor (err=%d)", ret);
		/* The init process may have aborted in the middle, force
		 * closing it. */
		sensor_close(dev, idev->udev);
		return ret;
	}

	idev->priv = dev;
	fpi_imgdev_open_complete(idev, 0);
	return 0;
}

/*
 * Device deinitialization.
 */
static void dev_deinit(struct fp_img_dev *idev)
{
	struct etes603_dev *dev = idev->priv;

	sensor_close(dev, idev->udev);
	idev->priv = NULL;

	libusb_release_interface(idev->udev, 0);
	fpi_imgdev_close_complete(idev);
}

static const struct usb_id id_table[] = {
	/* EgisTec (aka Lightuning) ES603 */
	{ .vendor = 0x1c7a, .product = 0x0603, .driver_data = 0x0603 },
	{ 0, 0, 0, },
};

struct fp_img_driver etes603_driver = {
	.driver = {
		.id = ETES603_ID,
		.name = FP_COMPONENT,
		.full_name = "EgisTec ES603",
		.id_table = id_table,
		.scan_type = FP_SCAN_TYPE_SWIPE,
	},
	.flags = 0,
	.img_height = -1,
	.img_width = -1, /* Once set it cannot be changed (except with -1) */

	.open = dev_init,
	.close = dev_deinit,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
};

