#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <libusb.h>


/* libusb defines */
#define EP_IN              0x81
#define EP_OUT             0x02
#define BULK_TIMEOUT       1000 /* Note that 1000 ms is usually enough but with CMD_READ_FP could be longer since the sensor is waiting motion. */

#define CMD_READ_REG       0x01
#define CMD_WRITE_REG      0x02
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

#define REG_MODE_SLEEP     0x30 /* Sleep mode */

#define REG_50             0x50 /* ? For contact detection */
#define REG_51             0x51 /* ? */
#define REG_59             0x59 /* ? */
#define REG_5A             0x5A /* ? */
#define REG_5B             0x5B /* ? */

#define REG_93             0x93 /* ? */
#define REG_94             0x94 /* ? */

libusb_context *fpi_usb_ctx = NULL;

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

struct libusb_device_handle;
struct fp_dev;
struct fp_img_dev {
	struct fp_dev *dev;
	struct libusb_device_handle *udev;

};

//	libusb_device_handle *udev;

static void debug_output(unsigned char ep, uint8_t *data, size_t size) {
	unsigned int i;

	if (ep == EP_OUT)
		fprintf(stderr, ">>> %lu bytes\n", size);
	else if (ep == EP_IN)
		fprintf(stderr, "<<< %lu bytes\n", size);

	for (i = 0; i < size; i++) {
		if (i != 0 && i % 16 == 0)
			fwrite("\n", 1, 1, stderr);
		fprintf(stderr, "%02X ", data[i]);
	}
	fwrite("\n", 1, 1, stderr);
}

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
		fprintf(stderr, "Bulk write error %s (%d)", libusb_error_name(ret), ret);
		return -1;
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
 * Read synchronously a register from the sensor.
 * Variadic argument pattern: int reg, int *val, ...
 */
static int dev_get_regs(libusb_device_handle *udev, int n_args, ... )
{
	va_list ap;
	struct egis_msg msg;
	int ret, i;

	if (n_args == 0 || n_args % 2 != 0 || n_args > REG_MAX * 2) {
		fprintf(stderr, "wrong number of arguments (%d)", n_args);
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
		fprintf(stderr, "sync_transfer EP_OUT failed");
		goto err;
	}
	ret = sync_transfer(udev, EP_IN, &msg, sizeof(msg));
	if (ret < 0) {
		fprintf(stderr, "sync_transfer EP_IN failed");
		goto err;
	}
	if (msg_header_check(&msg)) {
		fprintf(stderr, "msg_header_check failed");
		goto err;
	}
	if (msg.cmd != CMD_OK) {
		fprintf(stderr, "CMD_OK failed");
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
		fprintf(stderr, "wrong number of arguments (%d)", n_args);
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
		fprintf(stderr, "sync_transfer EP_OUT failed");
		goto err;
	}
	ret = sync_transfer(udev, EP_IN, &msg, sizeof(msg));
	if (ret < 0) {
		fprintf(stderr, "sync_transfer EP_IN failed");
		goto err;
	}
	if (msg_header_check(&msg)) {
		fprintf(stderr, "msg_header_check failed");
		goto err;
	}
	if (msg.cmd != CMD_OK) {
		fprintf(stderr, "CMD_OK failed");
		goto err;
	}
	return 0;
err:
	return -1;
}

/*
 * Change the mode of the sensor.
 */
static int set_mode_control(struct fp_img_dev *dev, uint8_t mode)
{
	if (dev_set_regs(dev->udev, 2, REG_MODE_CONTROL, mode))
		return -1;
	return 0;
}

static int get_cmd60(struct fp_img_dev *dev, uint8_t cmd, uint8_t val,
	uint8_t *reg)
{
	struct egis_msg msg;
	int ret;

	msg_header_prepare(&msg);
	msg.cmd = CMD_60;
	/* cmd 0x01: read / cmd 0x02: write */
	msg.sige_misc.val[0] = cmd;
	msg.sige_misc.val[1] = val;

	ret = sync_transfer(dev->udev, EP_OUT, &msg, MSG_HDR_SIZE + cmd);
	if (ret < 0) {
		fprintf(stderr, "sync_transfer EP_OUT failed");
		goto err_io;
	}
	ret = sync_transfer(dev->udev, EP_IN, &msg, sizeof(msg));
	if (ret < 0) {
		fprintf(stderr, "sync_transfer EP_IN failed");
		goto err_io;
	}
	if (msg_header_check(&msg)) {
		fprintf(stderr, "msg_header_check failed");
		goto err_cmd;
	}
	if (msg.cmd != CMD_OK) {
		fprintf(stderr, "CMD_OK failed");
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

/* external interface for testing */
struct fp_img_dev *global_init(void)
{
	int ret;
	struct fp_img_dev *dev;

	ret = libusb_init(&fpi_usb_ctx);
	if (ret != LIBUSB_SUCCESS) {
		fprintf(stderr, "libusb_init failed %d", ret);
		goto cantclaim;
	}

	dev = malloc(sizeof(struct fp_img_dev));
	if (dev == NULL) {
		fprintf(stderr, "cannot allocate memory");
		goto cantclaim;
	}

	dev->udev = libusb_open_device_with_vid_pid(fpi_usb_ctx, 0x1c7a, 0x0603);
	if (dev->udev == NULL) {
		fprintf(stderr, "libusb_open_device_with_vid_pid failed");
		free(dev);
		dev = NULL;
		goto cantclaim;
	}

cantclaim:
	return dev;
}

void global_exit(struct fp_img_dev * dev)
{
	if (dev->udev) {
		libusb_close(dev->udev);
		dev->udev = NULL;
	}

	/* TODO how to test fpi_usb_ctx */
	libusb_exit(fpi_usb_ctx);
}

#define WAIT_TIME 300000

int main()
{
	int i;
	struct fp_img_dev * dev = global_init();

	printf("*** Setting sleep mode ***\n");
	if (set_mode_control(dev, REG_MODE_SLEEP)) {
		fprintf(stderr, "cannot set sleep mode\n");
		return -1;
	}
	printf("*** Testing CMD 0x60 for few sec ***\n");
	for (i = 0; i < 3; i++) {
		printf("**** 00 LED Off\n");
		fflush(NULL);
		if (get_cmd60(dev, 0x02, 0x00, NULL))
			fprintf(stderr, "60 02 00 Failed\n");
		usleep(WAIT_TIME*(i+1));

		printf("**** 10 LED Red\n");
		fflush(NULL);
		if (get_cmd60(dev, 0x02, 0x10, NULL))
			fprintf(stderr, "60 02 10 Failed\n");
		usleep(WAIT_TIME*(i+1));

		printf("***** 11 ???\n");
		fflush(NULL);
		if (get_cmd60(dev, 0x02, 0x11, NULL))
			fprintf(stderr, "60 02 11 Failed\n");
		usleep(WAIT_TIME*(i+1));

		printf("***** 20 LED Blue\n");
		fflush(NULL);
		if (get_cmd60(dev, 0x02, 0x20, NULL))
			fprintf(stderr, "60 02 20 Failed\n");
		usleep(WAIT_TIME*(i+1));

		printf("***** 21 ???\n");
		fflush(NULL);
		if (get_cmd60(dev, 0x02, 0x21, NULL))
			fprintf(stderr, "60 02 21 Failed\n");
		usleep(WAIT_TIME*(i+1));

		printf("***** 30 LED Blue+Red\n");
		fflush(NULL);
		if (get_cmd60(dev, 0x02, 0x30, NULL))
			fprintf(stderr, "60 02 30 Failed\n");
		usleep(WAIT_TIME*(i+1));

		printf("***** 31 ???\n");
		fflush(NULL);
		if (get_cmd60(dev, 0x02, 0x31, NULL))
			fprintf(stderr, "60 02 31 Failed\n");
		usleep(WAIT_TIME*(i+1));
	}

	printf("***** 00 LED Off\n");
	if (get_cmd60(dev, 0x02, 0x00, NULL))
		fprintf(stderr, "60 02 00 Failed\n");

#if 0
// To test 
45 47 49 53 09 02 01 04 00

45 47 49 53 09 02 01 10 92
45 47 49 53 09 02 01 10 92

45 47 49 53 09 02 01 1a 04
45 47 49 53 09 02 01 1a 04

45 47 49 53 09 02 01 59 18
45 47 49 53 09 02 01 5a 08
45 47 49 53 09 02 01 5b 10 
#endif



	printf("*** Closing device ***\n");
	global_exit(dev);
	return 0;
}


