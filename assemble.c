#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

static XImage *image = NULL;
static uint32_t * currentPtr;
static const int image_width = 192;
static const int image_height = 800;
static char strFilenameDefault[] = "img.bin";
static char *strFilename;

static  Display *dis;
static  Window win;
static  Window parent;
static  GC gc;

#define FRAME_SIZE 384

/* Return the brightness of a frame */
static unsigned int process_get_brightness(uint8_t *f)
{
	int i;
	unsigned int sum = 0;
        for (i = 0; i < FRAME_SIZE; i++) {
                sum += f[i] & 0x0F;
                sum += f[i] >> 4;
        }
	return sum;
}

/* Return the number of found lines in new */
static int process_find_dup(uint8_t *dst, uint8_t *src)
{
	int i,j,v;

	unsigned int sum_error;
	unsigned int nb = 4;
	/* TODO min_error must be found via image brightness */
	unsigned int min_error = process_get_brightness(src) / 6; /*5000;*/ /* This value is found by tests */
	/* 384 bytes / 196 px width / 4 bits value */
	/* Scan 4 lines, assuming first that all 4 lines match. */
printf("sum_error=");
	for (i = 0; i < 4; i++) {
		sum_error = 0;
		for (j = 0; j < 96 * (4 - i); j++) {
			/* subtract */
			v = (int)(dst[j] & 0x0F) - (int)(src[j] & 0x0F);
			/* no need for abs since it uses ^2 */
			v = v >= 0 ? v : -v;
			sum_error += v /* * v*/;
			v = (int)(dst[j] >> 4) - (int)(src[j] >> 4);
			v = v >= 0 ? v : -v;
			sum_error += v /** v*/;
		}

		sum_error *= 127; /* increase value before divide (use int and value will be small) */
printf("%8d (%8d) ", sum_error / j, sum_error );
		sum_error = sum_error / j; /* Avg error/pixel */
		if (sum_error < min_error) {
			min_error = sum_error;
			nb = i;
			//break;
		}
		dst += 96; /* Next line */
	}
printf("-> newline=%d brightness=%d\n", nb, process_get_brightness(src));
	return nb;
}

/*
 * first 'merge' bytes from src and dst are merged then raw copy.
 */
static void merge_and_append(uint8_t *dst, uint8_t *src, size_t merge)
{
	size_t i;
	uint8_t pxl, pxh;
	/* Sanity checks */
	if (merge > FRAME_SIZE)
		merge = FRAME_SIZE;
	for (i = 0; i < merge; i++) {
		pxl = ((dst[i] & 0x0F) + (src[i] & 0x0F)) >> 1;
		pxh = ((dst[i] >> 4) + (src[i] >> 4)) >> 1;
		dst[i] = pxl | (pxh << 4);
	}
	for (i = merge; i < FRAME_SIZE; i++) {
		dst[i] = src[i];
	}
}

int merge(uint8_t *img, size_t img_sz)
{
  FILE *f;
  int line_cpy = 0;
  uint8_t *file_data, *src, *dst;
  size_t file_sz;
  //int processed_frame = 0;

  dst = img;
  /* Load img */
  f = fopen(strFilename, "r");
  if (!f)
    return 1;
  /* Get file size */
  fseek(f, 0L, SEEK_END);
  file_sz = ftell(f);
  fseek(f, 0L, SEEK_SET);
  /* allocate enough memory for file */
  src = file_data = malloc(file_sz);
  /* Read file */
  fread(file_data, 1, file_sz, f);
  fclose(f);

  /* Go further in the data */
  //src += 384*1400;
  /* Copy first frame */
  memcpy(dst, src, 384);
  src += 384;


  while (dst+384+384 <= img+img_sz && src+384 <= file_data+file_sz) {
    line_cpy = process_find_dup(dst, src);
    //line_cpy = 4;
    if (line_cpy > 0) {
      //memcpy(dst+384, src, line_cpy*96);
      dst += line_cpy * 96;
      merge_and_append(dst, src, (4-line_cpy)*96);
    }
    src+=384;
    //if (processed_frame++ > 60)
    //  break;
  }

/*
  line_cpy = process_find_dup(dst, src);
  printf("line_cpy %d\n", line_cpy);
  memcpy(dst+384, src, 4*96);
*/
  free(file_data);
  return 0;
}

/* Transform binary to image */
void transform (unsigned char *input, unsigned int input_size, uint32_t *output)
{
  unsigned int i, j = 0;
  for (i = 0; i < input_size; i++, j += 2)
    {
      unsigned char dl = input[i] << 4;
      unsigned char dh = input[i] & 0xF0;
      output[j]   = dl | (dl << 8) | (dl << 16);
      output[j+1] = dh | (dh << 8) | (dh << 16);
    }
}


void draw (Display *dis, Window win, GC gc)
{
  size_t read_size = 64000;
  unsigned char *bin;

  /* Create image */
  if (image == NULL) {
    image = XGetImage(dis, win, 0, 0, image_width, image_height, AllPlanes, ZPixmap);
    image->data = malloc (image_width*image_height*4);
    memset (image->data, 0, image_width*image_height*4);

    currentPtr = image->data;
    bin = malloc(64000);
    merge(bin, 64000);

    transform (bin, read_size, (uint32_t *)image->data);

  }

  XPutImage(dis, win, gc, image, 0, 0, 0, 0, image_width, image_height);
  //XDrawRectangle (dis, win, gc, 1, 1, 497, 497);
  //XDrawRectangle (dis, win, gc, 50, 50, 398, 398);
  XFlush (dis);
}


int main (int argc, char *argv[])
{
  int screen_number;
  XEvent event;

  if (argc >= 2) {
    strFilename = argv[1];
  }
  else {
    strFilename = strFilenameDefault;
  }
  /* Multi-thread app */
  XInitThreads();
  dis = XOpenDisplay (NULL);
  if (NULL == dis) {
    fprintf (stderr, "unable to open display\n");
    return EXIT_FAILURE;
  }

  screen_number = DefaultScreen (dis);
  parent = RootWindow (dis, screen_number);
  win = XCreateSimpleWindow (dis, parent, 1, 1, image_width, image_height, 0, BlackPixel (dis, 0), BlackPixel (dis, 0));
  if (win == None)
    {
      fprintf (stderr, "unable to create window\n");
      return EXIT_FAILURE;
    }

  gc = XCreateGC (dis, win, 0, NULL);
  if (gc == NULL)
    {
      fprintf (stderr, "unable to allocate GC\n");
      return EXIT_FAILURE;
    }

  XSelectInput (dis, win, ExposureMask | KeyPressMask | ButtonPressMask);
  XMapWindow (dis, win);
  XFlush (dis);


  while (1)
    {
      XNextEvent (dis, &event);

      switch (event.type)
        {
          case ButtonPress:
            /* The man page for XButtonEvent documents this. */
            printf ("You pressed button %d\n", event.xbutton.button);
            break;
          case Expose:
            printf ("Redrawing from Expose.\n");
            draw (dis, win, gc);
            break;
          case MapNotify:
            printf ("MapNotify Event.\n");
            break;
          case KeyPress:
            /* Close the program if q is pressed. */
            printf ("KeyPress event %d key %d\n", event.type, ((XKeyEvent)event.xkey).keycode );
            /*modifyLive (dis, win, gc);*/
            if (XK_q == XLookupKeysym (&event.xkey, 0))
              goto finishing;
            break;
          default:
            printf ("Unknown event %d\n", event.type);
            break;
        }
    }

finishing:

  /* For some reason the event loop stopped before q was pressed. */
  return EXIT_FAILURE;
}


