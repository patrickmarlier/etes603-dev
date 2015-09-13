#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <libusb.h>
#include "fp_fake.h"

static libusb_context *fpi_usb_ctx = NULL;

/* Same as global_init but without device initialization */
static struct fp_img_dev *global_open(void)
{
	int ret;
	struct fp_img_dev *dev;

	ret = libusb_init(&fpi_usb_ctx);
	if (ret != LIBUSB_SUCCESS) {
		fprintf(stderr, "libusb_init failed %d\n", ret);
		goto cantclaim;
	}

	dev = malloc(sizeof(struct fp_img_dev));
	if (dev == NULL) {
		fprintf(stderr, "cannot allocate memory\n");
		goto cantclaim;
	}

	dev->udev = libusb_open_device_with_vid_pid(fpi_usb_ctx, 0x1c7a, 0x0603);
	if (dev->udev == NULL) {
		fprintf(stderr, "libusb_open_device_with_vid_pid failed\n");
		free(dev);
		dev = NULL;
		goto cantclaim;
	}

cantclaim:
	return dev;
}

static void global_close(struct fp_img_dev * dev)
{
	if (dev->udev) {
		libusb_close(dev->udev);
		dev->udev = NULL;
	}

	/* TODO how to test fpi_usb_ctx */
	libusb_exit(fpi_usb_ctx);
}

#define WAIT_TIME 10000

static int reg_list[256];
static int reg_ind = 0;

static void reg_add(int reg)
{
	reg_list[reg_ind++] = reg;
}

static void reg_add_range(int start, int end)
{
	int i;
	for (i = start; i <= end; i++) {
		reg_add(i);
	}
}

int main()
{
	int reg;
	int value;
	int i;
	struct fp_img_dev * dev = global_open();

	if (dev == NULL) {
		fprintf(stderr, "Cannot open device\n");
		return 1;
	}

	/* Prepare list of registers to dump */
	reg_add_range(0x02, 0x04);
	reg_add(0x10);
	reg_add(0x1A);
	reg_add_range(0x20, 0x37);
	reg_add_range(0x41, 0x48);
	reg_add_range(0x50, 0x51);
	reg_add_range(0x59, 0x5B);
	reg_add_range(0x70, 0x73);
	reg_add_range(0x93, 0x94);
	reg_add_range(0xE0, 0xE6);
	reg_add(0xF0);
	reg_add(0xF2);

	for (i = 0; i < reg_ind; i++) {
		reg = reg_list[i];
		value = 0;
		if (dev_get_regs(dev->udev, 2, reg, &value)) {
			fprintf(stderr, "Failed reading reg %x\n", reg);
			continue;
		}
		printf("reg 0x%02x=0x%02x\n", reg, value);
		usleep(WAIT_TIME);
	}

	global_close(dev);
	return 0;
}


