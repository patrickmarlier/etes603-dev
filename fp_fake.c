#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <libusb.h>
#include "fp_fake.h"

#define UNUSED __attribute__((unused))

libusb_context *fpi_usb_ctx = NULL;

void fpi_imgdev_open_complete (struct fp_img_dev *imgdev UNUSED, int status UNUSED)
{
}

void fpi_imgdev_close_complete (struct fp_img_dev *imgdev UNUSED)
{
}

void fpi_imgdev_session_error(struct fp_img_dev *imgdev UNUSED, int error UNUSED)
{
}

void fpi_log(enum fpi_log_level level, const char *component,
        const char *function, const char *format, ...)
{
	va_list args;
	FILE *stream = stdout;
	const char *prefix;

	switch (level) {
		case LOG_LEVEL_INFO:
			prefix = "info";
			break;
		case LOG_LEVEL_WARNING:
			stream = stderr;
			prefix = "warning";
			break;
		case LOG_LEVEL_ERROR:
			stream = stderr;
			prefix = "error";
			break;
		case LOG_LEVEL_DEBUG:
			stream = stderr;
			prefix = "debug";
			break;
		default:
			stream = stderr;
			prefix = "unknown";
			break;
	}

	fprintf(stream, "%s:%s [%s] ", component ? component : "fp", prefix,
			function);

	va_start (args, format);
	vfprintf(stream, format, args);
	va_end (args);

	fprintf(stream, "\n");
}

void fpi_imgdev_activate_complete(struct fp_img_dev *imgdev UNUSED, int i UNUSED)
{
}

void fpi_imgdev_deactivate_complete(struct fp_img_dev *imgdev UNUSED)
{
}

struct fp_img *fpi_img_new(size_t length)
{
	struct fp_img *img = malloc(sizeof(*img) + length);
	memset(img, 0, sizeof(*img));
	img->length = length;
	return img;
}

void fpi_imgdev_image_captured(struct fp_img_dev *imgdev UNUSED, struct fp_img *img UNUSED)
{
}

void fpi_imgdev_report_finger_status(struct fp_img_dev *imgdev UNUSED, gboolean present UNUSED)
{
}

/* external interface for testing */
struct fp_img_dev *global_init(void)
{
	int ret;
	struct fp_img_dev *dev;

	ret = libusb_init(&fpi_usb_ctx);
	if (ret != LIBUSB_SUCCESS) {
		fp_err("libusb_init failed %d", ret);
		goto cantclaim;
	}

	dev = malloc(sizeof(struct fp_img_dev));
	if (dev == NULL) {
		fp_err("cannot allocate memory");
		goto cantclaim;
	}

	dev->udev = libusb_open_device_with_vid_pid(fpi_usb_ctx, 0x1c7a, 0x0603);
	if (dev->udev == NULL) {
		fp_err("libusb_open_device_with_vid_pid failed");
		free(dev);
		dev = NULL;
		goto cantclaim;
	}

	if (dev_init(dev, 0x0603))
		dev = NULL;

cantclaim:
	return dev;
}

void global_exit(struct fp_img_dev * dev)
{
	dev_deinit(dev);

	if (dev->udev) {
		libusb_close(dev->udev);
		dev->udev = NULL;
	}

	/* TODO how to test fpi_usb_ctx */
	libusb_exit(fpi_usb_ctx);
}

#if 0
int main()
{
	global_init();
	/* TODO add other things here? */
	global_exit();

	return 0;
}
#endif
