CC           := gcc
CPPFLAGS     :=
CFLAGS       := -Wall -Wextra -O0 -ggdb3
LDFLAGS      := -L/usr/lib/x86_64-linux-gnu -lpthread

USB_CPPFLAGS := -I/usr/include/libusb-1.0
USB_CFLAGS   :=
USB_LDFLAGS  := -lusb-1.0
X11_DIR      := /usr/X11R6
X11_CPPFLAGS += -I$(X11_DIR)
X11_LDFLAGS  += -L$(X11PREFIX)/lib -lX11 -Wl,-rpath=$(X11PREFIX)/lib
FP_DIR       := ../libfprint/libfprint
FP_CPPFLAGS  := -I$(FP_DIR) -I../libfprint-build
FP_LDFLAGS   :=

GLIB_CFLAGS  := $(shell pkg-config --cflags glib-2.0 gtk+-2.0)
GLIB_LDFLAGS := $(shell pkg-config --libs glib-2.0 gtk+-2.0)

.PHONY: all clean
BINS = gui contact assemble leds dumpregs

all: $(BINS)

assemble: assemble.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(X11_LDFLAGS) -lm

contact: etes603.o contact.o fp_fake.o
	$(CC) -o $@ $^ $(LDFLAGS) -I. $(USB_LDFLAGS)

dumpregs: dumpregs.o etes603.o fp_fake.o
	$(CC) -o $@ $^ $(LDFLAGS) $(USB_LDFLAGS)

gui: etes603.o gui.o fp_fake.o
	$(CC) -o $@ $^ $(LDFLAGS) $(X11_LDFLAGS) $(USB_LDFLAGS)

leds: leds.c
	$(CC) $(CPPFLAGS) $(USB_CPPFLAGS) $(CFLAGS) -o $@ $^ $(LDFLAGS) -lm $(USB_LDFLAGS)

dumpregs.o: dumpregs.c
	$(CC) $(CPPFLAGS) $(USB_CPPFLAGS) $(FP_CPPFLAGS) $(CFLAGS) $(GLIB_CFLAGS) -c -o $@ $<

fp_fake.o: fp_fake.c fp_fake.h
	$(CC) $(CPPFLAGS) $(USB_CPPFLAGS) $(FP_CPPFLAGS) $(CFLAGS) $(GLIB_CFLAGS) -c -o $@ $<

# Globalize symbols (etes603.c had static functions, ie no external API, for inclusion in libfprint)
etes603.o: etes603.c
	$(CC) $(CPPFLAGS) $(USB_CPPFLAGS) $(FP_CPPFLAGS) $(CFLAGS) $(GLIB_CFLAGS) -c -o $@ $<
	objcopy -w --globalize-symbol=image_capture\* --globalize-symbol=frame_\* --globalize-symbol=process_frame\* --globalize-symbol=sync_\* --globalize-symbol=dev_\* --globalize-symbol=contact_\* --globalize-symbol=get_\* --globalize-symbol=fp_\* $@

gui.o: gui.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(X11_CPPFLAGS) -c -o $@ $<

clean:
	$(RM) $(BINS) *.o

