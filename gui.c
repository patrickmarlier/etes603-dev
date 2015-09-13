#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/cursorfont.h>

#include "fp_fake.h"

static char strFilenameDefault[] = "img.bin";
static char *strFilename;

static Display *dis;
static Window parent;
static Window mainwin;
static const int win_width = 500;
static const int win_height = 600;

static Window draw_win;
static GC draw_gc;
static XImage *image = NULL;
static uint32_t * currentPtr;
static const int image_width = 256/*192*/;
static const int image_height = 600;

#define BUTTON_WIDTH 60
#define BUTTON_HEIGHT 20
#define BUTTON_MASK ExposureMask | EnterWindowMask | LeaveWindowMask | ButtonPressMask | ButtonReleaseMask
static Window button_quit;
static char* quitLabel = "Quit";

static Window button_modes[4];
static volatile int capture_mode = 0;

#define SB_LONGLABEL 128
#define SB_WIDTH 80
#define SB_HEIGHT 20
struct {
   Window    window;
   char      label[SB_LONGLABEL];
   int       (*func)(struct fp_img_dev *);
   char      active;
   int       pid;
} MenuButton[128];

static Cursor mycursor;
static XFontStruct * font_info;
static const char * fontname = "9x15";
static XSetWindowAttributes setwinattr;
static long fg, bg;

static GC gc;
static volatile int stop = 0;

#define FRAME_WIDTH 0xC0  /* 192 */


/* Transform binary to image */
void transform(unsigned char *input, unsigned int input_size, uint32_t *output, size_t output_size)
{
  unsigned int i, j = 0;
  for (i = 0; i < input_size && j < output_size; i++, j += 2)
    {
      unsigned char dl = input[i] << 4;
      unsigned char dh = input[i] & 0xF0;
      output[j]   = dh | (dh << 8) | (dh << 16);
      output[j+1] = dl | (dl << 8) | (dl << 16);
      //if ((i != 0) && ((i % (FRAME_WIDTH/2)) == (FRAME_WIDTH/2)-1)) {
      //  j += (256-FRAME_WIDTH);
      if ((i != 0) && ((i % 96) == 95)) {
        j += (256-192);
      //if ((i != 0) && ((i % 48) == 47)) {
      //  j += (256-96);
      }
    }
}

void transform2(unsigned char *input, unsigned int input_size, uint32_t *output)
{
  unsigned int i, j = 0;
  for (i = 0; i < input_size; i++, j += 2)
    {
      unsigned char dl = input[i] << 4;
      unsigned char dh = input[i] & 0xF0;
      output[j+1]   = dl | (dl << 8) | (dl << 16);
      output[j] = dh | (dh << 8) | (dh << 16);
    }
}

void modifyLive(Display *dis, Window win, GC gc)
{
  /* TODO draw is almost a duplicate */
  XPutImage(dis, win, gc, image, 0, 0, 0, 0, image_width, image_height);
  XFlush(dis);
}

#if 0
void append ( uint32_t *output, int size)
{
  memcpy (currentPtr, output, size * 4 * 2);
  currentPtr += (size * 2);
  if (currentPtr >= (image->data + image_width*image_height*4))
    currentPtr = image->data;
}
#endif

void draw(Display *dis, Window win, GC gc)
{
  //XDrawRectangle (dis, win, gc, 50, 50, 398, 398);

  if (image == NULL) {
    FILE *bitmap;
    size_t read_size;
    unsigned char *bin;

    /* Create image */
    image = XGetImage(dis, win, 0, 0, image_width, image_height, AllPlanes, ZPixmap);
    image->data = malloc(image_width * image_height * 4);
    /* Fill with white pixels */
    memset(image->data, 0xFF, image_width * image_height * 4);

    currentPtr = (uint32_t *)image->data;
    /* Open raw file */
    bitmap = fopen(strFilename, "r");
    if (bitmap) {
      /* Read up to 64000 bytes */
      bin = malloc(64000);
      read_size = fread(bin, sizeof(char), 64000, bitmap);
      fclose(bitmap);
      /* transform to fit the bitmap format  */
      transform(bin, read_size, (uint32_t *)image->data, image_width * image_height);
    }
  }

  /* Display the new image */
  XPutImage(dis, win, gc, image, 0, 0, 0, 0, image_width, image_height);
  //XSetForeground(dis, gc, 0xFF0000);
  //XFillRectangle(dis, win, gc, 1, 1, 10, 10);
  /* Force output now */
  XFlush(dis);
}

static unsigned int liminosity(uint8_t *d, int size)
{
  int i;
  unsigned int sum = 0;
  for (i = 0; i < size; i++) {
    sum += d[i];
  }
  return sum;
}

void *thread_entry(void *arg)
{
  struct fp_img_dev *idev = (struct fp_img_dev *)arg;
  //struct libusb_device_handle *udev = idev->udev;
  struct etes603_dev *dev = (struct etes603_dev *)idev->priv;
  uint8_t *braw = malloc(256000);
  uint8_t *brawp = braw;
  uint8_t *bframe = malloc(256000);
  uint8_t *bimg = malloc(image_width*image_height*4*2);
  XExposeEvent event = { Expose, 0, 1, dis, draw_win, 0, 0, image_width, image_height, 0 };

  printf("Capturing thread started...\n");

//#define MODE_FRAME
//#define MODE_IMAGE
//#define MODE_LOGGING
#define MODE_IMAGE_FULL

  frame_prepare_capture(dev);
  if (frame_capture(dev, bframe)) {
    printf("frame_capture failed\n");
    return (void *)1;
  }

#ifdef MODE_IMAGE_FULL
//  if (fp_capture(dev, bframe, 64000))
//    return (void *)1;
  frame_prepare_capture(dev);
#endif


  while (stop == 0) {
#ifdef MODE_FRAME
    frame_capture(dev, bframe);
    if (process_frame_empty(bframe, FRAME_WIDTH * 2, 1))
      continue;

    do {
      brawp = process_frame(brawp, bframe/*, FRAME_WIDTH*/);
      //memcpy(brawp, bframe, FRAME_WIDTH * 2);
      //brawp += 384 /*FRAME_WIDTH * 2*/;
      if (brawp + 384 >= braw + 256000)
        break;
      frame_capture(dev, bframe);
      /* Realtime... */
      transform(braw, 96000, (uint32_t *)bimg, image_width * image_height);
      if (image != NULL) {
        memcpy(image->data, bimg, image_width * image_height * 4);
      }
      XSendEvent(dis, draw_win /* mainwin*/, False, 0 /*ExposureMask*/, (XEvent *)&event);
      XFlush(dis);

      if (stop)
        goto leave_loop;
    } while (!process_frame_empty(bframe, FRAME_WIDTH * 2, 1));
    transform(braw, 96000, (uint32_t *)bimg, image_width * image_height);
#endif

#ifdef MODE_IMAGE
    if (fp_capture(dev, bframe, 256000))
      stop = 1;
    braw = bframe;
    memset(bimg, 0, 64000*2*4);
    transform(braw, 96000, (uint32_t *)bimg, image_width * image_height);
#endif

#ifdef MODE_IMAGE_FULL
    frame_capture(dev, bframe);
    if (process_frame_empty(bframe, FRAME_WIDTH * 2, 1))
      continue;

    //fp_capture(dev, bframe);
    if (fp_capture(dev, bframe, 64000))
    //if (sync_read_buffer_full(udev, bframe))
      stop = 1;
    memcpy(braw, bframe, 64000);
    if (fp_capture(dev, bframe, 64000))
    //if (sync_read_buffer_full(udev, bframe))
      stop = 1;
    memcpy(braw+64000, bframe, 64000);
    memset(bimg, 0, 64000*2*4);
    transform2(braw, 128000, (uint32_t *)bimg);
    frame_prepare_capture(dev);
#endif

#ifdef MODE_LOGGING
    static int index = 0;
    char filename[255];
    frame_capture(dev, bframe);
    if (process_frame_empty(bframe, FRAME_WIDTH * 2, 1))
      continue;
    sprintf (filename, "/tmp/scan%03d", index++);
    FILE *f = fopen(filename, "w");
    if (!f) perror("fopen");
    do {
      brawp = process_frame(brawp, bframe);
      frame_capture(dev, bframe);
      /* writing file */
      if (f)
        fwrite (bframe, 1, 384, f);

      if (stop)
        goto leave_loop;
    } while (!process_frame_empty(bframe, FRAME_WIDTH * 2, 1));    /* Saving all frames to files */
    transform(braw, 96000, (uint32_t *)bimg, image_width * image_height);
    if (f) {
      fclose(f);
      f = NULL;
    }
#endif

    if (image != NULL) {
      memcpy(image->data, bimg, image_width * image_height * 4);
    }
    memset(braw, 0, 256000);
    brawp = braw;

    XSendEvent(dis, draw_win /* mainwin*/, False, 0 /*ExposureMask*/, (XEvent *)&event);
    XFlush(dis);
  } /* stop == 0 */

#if defined(MODE_LOGGING) || defined(MODE_FRAME)
 leave_loop:
#endif
  printf("Capturing thread leaving...\n");
  return NULL;
}

void MakeButton(int x, int y, char * label,int (*fun)(struct fp_img_dev *), int id)
{
   Cursor tempcursor;

   strncpy(MenuButton[id].label, label, SB_LONGLABEL);
   MenuButton[id].func = fun;
   MenuButton[id].active = 0;
   MenuButton[id].window = XCreateSimpleWindow(dis, mainwin, x, y, SB_WIDTH, SB_HEIGHT, 1, fg, bg);
   XSelectInput(dis, MenuButton[id].window, BUTTON_MASK);
   tempcursor = XCreateFontCursor(dis, XC_hand1);
   XDefineCursor(dis, MenuButton[id].window, tempcursor);
   setwinattr.backing_store = Always;
   XChangeWindowAttributes(dis, MenuButton[id].window, CWBackingStore, &setwinattr);
   XMapWindow(dis, MenuButton[id].window);
}

void HandleRegButton(XEvent * event, int id, struct fp_img_dev *dev)
{
  int width, center;
  switch(event->type) {
    case Expose:
    case LeaveNotify:
      XClearWindow(dis, MenuButton[id].window);
      width = XTextWidth(font_info, MenuButton[id].label, strlen(MenuButton[id].label));
      center = (SB_WIDTH - width) / 2;
      XDrawString(dis, MenuButton[id].window, gc, center, font_info->ascent, MenuButton[id].label, strlen(MenuButton[id].label));
      XFlush(dis);
      break;
    case EnterNotify:
    case ButtonPress:
      XDrawRectangle(dis, MenuButton[id].window, gc, 1, 1, SB_WIDTH - 3, SB_HEIGHT - 3);
      break;
    case ButtonRelease:
      MenuButton[id].func(dev);
      break;
    default:
      break;
  }
}

void HandleDraw(XEvent* event)
{
  switch(event->type) {
    case Expose:
      //printf("Redrawing from Expose.\n");
      draw(dis, draw_win, draw_gc);
      break;
    default:
      break;
  }
}

/*event->xbutton.button*/

int HandleQuitButton(XEvent* event)
{
  int must_exit = 0;
  int width, center;
  switch(event->type) {
    case Expose:
    case LeaveNotify:
      XClearWindow(dis, button_quit);
      width = XTextWidth(font_info, quitLabel, strlen(quitLabel));
      center = (BUTTON_WIDTH - width) / 2;
      XDrawString(dis, button_quit, gc, center, font_info->ascent/*BUTTON_HEIGHT / 2*/, quitLabel, strlen(quitLabel));
      XFlush(dis);
      break;
    case EnterNotify:
    case ButtonPress:
      XDrawRectangle(dis, button_quit, gc, 1, 1, BUTTON_WIDTH - 3, BUTTON_HEIGHT - 3);
      break;
    case ButtonRelease:
      must_exit = 1;
      break;
    default:
      break;
  }
  return must_exit;
}

void HandleModeButtons(XEvent* event, int mode)
{
  int width, center;
  switch(event->type) {
    case Expose:
    case LeaveNotify:
      XClearWindow(dis, button_modes[mode]);
      width = XTextWidth(font_info, "Mode", strlen("Mode"));
      center = (BUTTON_WIDTH - width) / 2;
      XDrawString(dis, button_modes[mode], gc, center, font_info->ascent, "Mode", strlen("Mode"));
      XFlush(dis);
      break;
    case EnterNotify:
    case ButtonPress:
      XDrawRectangle(dis, button_modes[mode], gc, 1, 1, BUTTON_WIDTH - 3, BUTTON_HEIGHT - 3);
      break;
    case ButtonRelease:
      capture_mode = mode;
      break;
    default:
      break;
  }
}

int AddReg(struct fp_img_dev *dev, uint8_t reg, int add, char *name, uint8_t min, uint8_t max)
{
  uint8_t oldv, newv;
  if (!dev)
    return 1;
  dev_get_regs(dev->udev, 2, reg, &oldv);
  newv = oldv + add;
  if (newv < min)
    newv = min;
  if (newv > max)
    newv = max;
  printf("%s (%02X) = %02X -> %02X\n", name, reg, oldv, newv);
  dev_set_regs(dev->udev, 2, reg, newv);
  return 0;
}

int IncReg03(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  //return AddReg(dev, 0x03, 1, "REG_03", 0, 0x35);
  dev_set_regs(dev->udev, 2, 0x03, 0x08);
  return 0;
}

int DecReg03(struct fp_img_dev *dev)
{
  return AddReg(dev, 0x03, -1, "REG_03", 0, 0x35);
}

int IncReg04(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0x04, 1, "REG_04", 0, 0x35);
}

int DecReg04(struct fp_img_dev *dev)
{
  return AddReg(dev, 0x04, -1, "REG_04", 0, 0x35);
}

int IncReg10(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0x10, 1, "REG_10", 0, 0x99);
}

int DecReg10(struct fp_img_dev *dev)
{
  return AddReg(dev, 0x10, -1, "REG_10", 0, 0x99);
}

int IncReg1A(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0x1A, 1, "REG_1A", 0, 0x35);
}

int DecReg1A(struct fp_img_dev *dev)
{
  return AddReg(dev, 0x1A, -1, "REG_1A", 0, 0x35);
}

int IncReg93(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0x93, 1, "REG_93", 0, 0x50);
}

int DecReg93(struct fp_img_dev *dev)
{
  return AddReg(dev, 0x93, -1, "REG_93", 0, 0x50);
}

int IncReg94(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0x94, 1, "REG_94", 0, 0x35);
}

int DecReg94(struct fp_img_dev *dev)
{
  return AddReg(dev, 0x94, -1, "REG_94", 0, 0x35);
}

int IncGain(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0xE0, 1, "GAIN", 0, 0x35);
}

int DecGain(struct fp_img_dev *dev)
{
  return AddReg(dev, 0xE0, -1, "GAIN", 0, 0x35);
}

int IncVRT(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0xE1, 1, "VRT", 0, 0x3F);
}

int DecVRT(struct fp_img_dev *dev)
{
  return AddReg(dev, 0xE1, -1, "VRT", 0, 0x3F);
}

int IncVRB(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0xE2, 1, "VRB", 0, 0x3A);
}

int DecVRB(struct fp_img_dev *dev)
{
  return AddReg(dev, 0xE2, -1, "VRB", 0, 0x3A);
}

int IncDTVRT(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0xE3, 1, "DTVRT", 0, 0x20);
}

int DecDTVRT(struct fp_img_dev *dev)
{
  return AddReg(dev, 0xE3, -1, "DTVRT", 0, 0x20);
}

int IncVCO_C(struct fp_img_dev *dev)
{
  /* TODO unknown min max */
  return AddReg(dev, 0xE5, 1, "VCO_C", 0, 0x20);
}

int DecVCO_C(struct fp_img_dev *dev)
{
  return AddReg(dev, 0xE5, -1, "VCO_C", 0, 0x20);
}

int IncDCOffset(struct fp_img_dev *dev)
{
  return AddReg(dev, 0xE6, 1, "DCOFFSET", 0, 0x35);
}

int DecDCOffset(struct fp_img_dev *dev)
{
  return AddReg(dev, 0xE6, -1, "DCOFFSET", 0, 0x35);
}



int main(int argc, char *argv[])
{
  pthread_t thread;
  int screen_number;
  int i;
  XEvent event;
  struct fp_img_dev *dev;

  if (argc >= 2) {
    strFilename = argv[1];
  } else {
    strFilename = strFilenameDefault;
  }
  /* Multi-thread app */
  XInitThreads();
  dis = XOpenDisplay(NULL);
  if (NULL == dis) {
    fprintf(stderr, "unable to open display\n");
    return EXIT_FAILURE;
  }

  screen_number = DefaultScreen (dis);
  //parent = RootWindow (dis, screen_number);
  parent = DefaultRootWindow(dis);

  fg = BlackPixel(dis, screen_number);
  bg = WhitePixel(dis, screen_number);

  mainwin = XCreateSimpleWindow(dis, parent, 0, 0, win_width, win_height, 0, fg, bg);
  if (mainwin == None) {
    fprintf (stderr, "unable to create window\n");
    return EXIT_FAILURE;
  }

  gc = XCreateGC(dis, mainwin, 0, NULL);
  if (gc == NULL) {
    fprintf (stderr, "unable to allocate GC\n");
    return EXIT_FAILURE;
  }

  // set up font
  if ((font_info = XLoadQueryFont(dis, fontname)) == NULL) {
    perror("XLoadQueryFont");
    exit(1);
  }
  XSetFont(dis, gc, font_info->fid);

  // Create Cursor for Buttons
  mycursor = XCreateFontCursor(dis, XC_hand1);

  // Quit Button
  button_quit = XCreateSimpleWindow(dis, mainwin, 300, 10, BUTTON_WIDTH, BUTTON_HEIGHT, 1, fg, bg);
  XChangeWindowAttributes(dis, button_quit, CWBackingStore, &setwinattr);
  XSelectInput(dis, button_quit, BUTTON_MASK);
  XDefineCursor(dis, button_quit, mycursor);
  XMapWindow(dis, button_quit);

  // Mode Buttons
  for (i = 0 ; i < 3; i++) {
    button_modes[i] = XCreateSimpleWindow(dis, mainwin, 300+i*65, 30, BUTTON_WIDTH, BUTTON_HEIGHT, 1, fg, bg);
    XChangeWindowAttributes(dis, button_modes[i], CWBackingStore, &setwinattr);
    XSelectInput(dis, button_modes[i], BUTTON_MASK);
    XDefineCursor(dis, button_modes[i], mycursor);
    XMapWindow(dis, button_modes[i]);
  }

  // Drawing area
  draw_win = XCreateSimpleWindow(dis, mainwin, 0, 0, image_width, image_height, 1, fg, bg);
  XSelectInput(dis, draw_win, BUTTON_MASK);
  XMapWindow(dis, draw_win);
  draw_gc = XCreateGC(dis, draw_win, 0, NULL);
  if (draw_gc == NULL) {
    fprintf (stderr, "unable to allocate Draw_win GC\n");
    return EXIT_FAILURE;
  }

  // Create tuning buttons
  int y = 60, id = 0;
  MakeButton(300, y, "IncReg03", IncReg03, id);
  MakeButton(400, y, "DecReg03", DecReg03, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncReg04", IncReg04, id);
  MakeButton(400, y, "DecReg04", DecReg04, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncReg10", IncReg10, id);
  MakeButton(400, y, "DecReg10", DecReg10, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncReg1A", IncReg1A, id);
  MakeButton(400, y, "DecReg1A", DecReg1A, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncReg93", IncReg93, id);
  MakeButton(400, y, "DecReg93", DecReg93, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncReg94", IncReg94, id);
  MakeButton(400, y, "DecReg94", DecReg94, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncGain", IncGain, id);
  MakeButton(400, y, "DecGain", DecGain, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncVRT", IncVRT, id);
  MakeButton(400, y, "DecVRT", DecVRT, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncVRB", IncVRB, id);
  MakeButton(400, y, "DecVRB", DecVRB, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncDTVRT", IncDTVRT, id);
  MakeButton(400, y, "DecDTVRT", DecDTVRT, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncVCO_C", IncVCO_C, id);
  MakeButton(400, y, "DecVCO_C", DecVCO_C, id+1);
  y += 20; id += 2;
  MakeButton(300, y, "IncDCOff", IncDCOffset, id);
  MakeButton(400, y, "DecDCOff", DecDCOffset, id+1);

  // XSelectInput (dis, mainwin, ExposureMask | KeyPressMask | ButtonPressMask);
  XMapWindow(dis, mainwin);
  XFlush(dis);




  dev = global_init();

  if (dev == NULL || dev->udev == NULL) {
    fprintf(stderr, "Cannot open device\n");
  }

  /* create thread */
  if (dev != NULL && dev->udev != NULL)
    pthread_create(&thread, NULL, thread_entry, (void*)dev);

  while (1)
    {
      if (XCheckWindowEvent(dis, button_quit, BUTTON_MASK, &event)) {
        if (HandleQuitButton(&event))
          goto finishing;
      }
      /* All modes buttons */
      for (i = 0; i < 3; i++) {
        if (XCheckWindowEvent(dis, button_modes[i], BUTTON_MASK, &event))
          HandleModeButtons(&event, i);
      }
      /* All regs buttons */
      for (i = 0; i < 128; i++) {
        if (XCheckWindowEvent(dis, MenuButton[i].window, BUTTON_MASK, &event))
          HandleRegButton(&event, i, dev);
      }
      /* TODO not BUTTON MASK but only? */
      if (XCheckWindowEvent(dis, draw_win, BUTTON_MASK, &event))
        HandleDraw(&event);
#if 0
      XNextEvent (dis, &event);

      switch (event.type)
        {
          case ButtonPress:
            /* The man page for XButtonEvent documents this. */
            printf ("You pressed button %d\n", event.xbutton.button);
            modifyLive (dis, win, gc);
            break;
          case Expose:
            //printf ("Redrawing from Expose.\n");
            draw (dis, win, gc);
            break;
          case MapNotify:
            printf ("MapNotify Event.\n");
            break;
          case KeyPress:
            /* Close the program if q is pressed. */
            printf ("KeyPress event %d key %d\n", event.type, ((XKeyEvent)event.xkey).keycode );
            modifyLive (dis, win, gc);
#define REG_TUNE 0xE6
            if (XK_o == XLookupKeysym (&event.xkey, 0)) {
                unsigned char reg = 0;
                dev_get_regs(dev->udev, 2, REG_TUNE, &reg);
                dev_set_regs(dev->udev, 2, REG_TUNE, ++reg);
                printf("%02x: %02x\n", REG_TUNE, reg);
            }
            if (XK_l == XLookupKeysym (&event.xkey, 0)) {
                int reg;
                dev_get_regs(dev->udev, 2, REG_TUNE, &reg);
                dev_set_regs(dev->udev, 2, REG_TUNE, --reg);
                printf("%02x: %02x\n", REG_TUNE, reg);
            }
            if (XK_q == XLookupKeysym (&event.xkey, 0))
              goto finishing;
            break;
          default:
            printf ("Unknown event %d\n", event.type);
            break;
        }
#endif
    }

finishing:
  stop = 1;
  usleep(500*1000);
  if (dev != NULL && dev->udev != NULL)
    global_exit(dev);

  /* For some reason the event loop stopped before q was pressed. */
  return EXIT_FAILURE;
}

