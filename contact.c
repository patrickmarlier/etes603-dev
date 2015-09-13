#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "fp_fake.h"

int contact_detect(struct etes603_dev *dev);
int contact_polling_init(struct etes603_dev *dev);
int contact_polling_exit(struct etes603_dev *dev);
int contact_polling(struct etes603_dev *dev);

int main()
{
  struct fp_img_dev *idev;
  struct etes603_dev *dev;

  idev = global_init();

  if (idev == NULL || idev->udev == NULL) {
    fprintf(stderr, "Cannot open device\n");
  }

  dev = idev->priv;

  if (contact_polling(dev) == 1) {
    printf("Contact detected\n");
  }

  if (idev != NULL && idev->udev != NULL)
    global_exit(idev);

  return 0;
}
