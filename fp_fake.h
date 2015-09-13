
enum fpi_log_level {
        LOG_LEVEL_DEBUG,
        LOG_LEVEL_INFO,
        LOG_LEVEL_WARNING,
        LOG_LEVEL_ERROR,
};

#ifndef FP_COMPONENT
#define FP_COMPONENT "etes603"
#endif

#ifdef ENABLE_LOGGING
#define _fpi_log(level, fmt...) fpi_log(level, FP_COMPONENT, __FUNCTION__, fmt)
#else
#define _fpi_log(level, fmt...)
#endif

#ifdef ENABLE_DEBUG_LOGGING
#define fp_dbg(fmt...) _fpi_log(LOG_LEVEL_DEBUG, fmt)
#else
#define fp_dbg(fmt...)
#endif

#define fp_info(fmt...) _fpi_log(LOG_LEVEL_INFO, fmt)
#define fp_warn(fmt...) _fpi_log(LOG_LEVEL_WARNING, fmt)
#define fp_err(fmt...) _fpi_log(LOG_LEVEL_ERROR, fmt)


struct libusb_device_handle;
struct fp_dev;
/* Need to be in sync with your current version of libfprint */
struct fp_img_dev {
	struct fp_dev *dev;
	struct libusb_device_handle *udev;
	int/*enum fp_imgdev_action*/ action;
	int action_state;

	struct fp_print_data *acquire_data;
	struct fp_print_data *enroll_data;
	struct fp_img *acquire_img;
	int enroll_stage;
	int action_result;

	/* FIXME: better place to put this? */
	size_t identify_match_offset;

	void *priv;
};

struct fp_img {
	int width;
	int height;
	size_t length;
	uint16_t flags;
	struct fp_minutiae *minutiae;
	unsigned char *binarized;
	unsigned char data[0];
};
typedef int gboolean;
struct etes603_dev;

int frame_capture(struct etes603_dev *dev, uint8_t *buf);
int frame_prepare_capture(struct etes603_dev *dev);

int fp_capture_asm(struct etes603_dev *dev, uint8_t *braw, int size);
int fp_capture(struct etes603_dev *dev, uint8_t *buf, size_t size);

uint8_t *process_frame(uint8_t *dst, uint8_t *src);
int process_frame_empty(uint8_t *f, size_t s, int mode);
int contact_detect(struct etes603_dev *dev);

int dev_set_regs(void *dev, int n_args, ... /*int reg, int val*/);
int dev_get_regs(void *dev, int n_args, ... /* int reg, int *val */);

int dev_init(struct fp_img_dev *idev, unsigned long driver_data);
void dev_deinit(struct fp_img_dev *idev);


/* fp_fake.c */
struct fp_img_dev *global_init(void);
void global_exit(struct fp_img_dev * dev);


