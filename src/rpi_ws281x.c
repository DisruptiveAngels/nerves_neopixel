#include <limits.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <err.h>

#include "rpi_ws281x/ws2811.h"
#include "base64.h"
#include "utils.h"

#define DMA_CHANNEL 10

typedef struct {
  uint16_t width;
  uint16_t height;
  uint16_t *topology;
} canvas_t;

int32_t min(int32_t a, int32_t b) {
  return (a < b) ? a : b;
}

int32_t max(int32_t a, int32_t b) {
  return (a > b) ? a : b;
}

void init_canvas(uint16_t width, uint16_t height, canvas_t *canvas) {
  debug("Called init_canvas(width: %hu, height: %hu)", width, height);
  canvas->width = width;
  canvas->height = height;
  if (canvas->topology != NULL)
    free(canvas->topology);
  canvas->topology = malloc(width * height * sizeof(uint16_t));
  // Initialize all offsets to USHRT_MAX
  memset(canvas->topology, 0xFF, width * height * sizeof(uint16_t));
}

void init_pixels(uint8_t channel, uint16_t offset, uint16_t x, uint16_t y, uint16_t count, int8_t dx, int8_t dy, canvas_t *canvas) {
  debug("Called init_pixels(channel: %hhu, offset: %hu, x: %hu, y: %hu, count: %hu, dx: %hhi, dy: %hhi)", channel, offset, x, y, count, dx, dy);
  if (offset + count - 1 >= 32767) // 0xEFFF
    errx(EXIT_FAILURE, "The offset of the last pixel in each channel must be less than 32767.");
  if (min(x, x + (count - 1) * dx) < 0 || max(x, x + (count - 1) * dx) >= canvas->width ||
      min(y, y + (count - 1) * dy) < 0 || max(y, y + (count - 1) * dy) >= canvas->height)
    errx(EXIT_FAILURE, "The pixels must all be within the bounds of the canvas in init_pixel command");
  // MSB designates which channel to use
  offset |= (channel << 15);
  uint16_t i;
  for (i = 0; i < count; i++) {
    debug("  Setting topology(%hu, %hu) to %hu", x, y, offset);
    canvas->topology[(canvas->width * y) + x] = offset++;
    x += dx;
    y += dy;
  }
}

int parse_strip_type(char *strip_type) {
  if (!strncasecmp("rgb", strip_type, 4))
    return WS2811_STRIP_RGB;
  else if (!strncasecmp("rbg", strip_type, 4))
    return WS2811_STRIP_RBG;
  else if (!strncasecmp("grb", strip_type, 4))
    return WS2811_STRIP_GRB;
  else if (!strncasecmp("gbr", strip_type, 4))
    return WS2811_STRIP_GBR;
  else if (!strncasecmp("brg", strip_type, 4))
    return WS2811_STRIP_BRG;
  else if (!strncasecmp("bgr", strip_type, 4))
    return WS2811_STRIP_BGR;
  else if (!strncasecmp("rgbw", strip_type, 4))
    return SK6812_STRIP_RGBW;
  else if (!strncasecmp("rbgw", strip_type, 4))
    return SK6812_STRIP_RBGW;
  else if (!strncasecmp("grbw", strip_type, 4))
    return SK6812_STRIP_GRBW;
  else if (!strncasecmp("gbrw", strip_type, 4))
    return SK6812_STRIP_GBRW;
  else if (!strncasecmp("brgw", strip_type, 4))
    return SK6812_STRIP_BRGW;
  else if (!strncasecmp("bgrw", strip_type, 4))
    return SK6812_STRIP_BGRW;
  else
    errx(EXIT_FAILURE, "Invalid strip type %s\n", strip_type);
}

void set_brightness(uint8_t channel, uint8_t brightness, ws2811_channel_t *channels) {
  debug("Called set_brightness(channel: %hhu, brightness: %hhu)", channel, brightness);
  if (channel > 1)
    errx(EXIT_FAILURE, "Channel must be 0 or 1.");
  channels[channel].brightness = brightness;
}

void set_gamma(uint8_t channel, uint8_t *gamma, ws2811_channel_t *channels) {
  debug("Called set_gamma(channel: %hhu, gamma: <binary>)", channel);
  if (channel > 1)
    errx(EXIT_FAILURE, "Channel must be 0 or 1.");
  channels[channel].gamma = gamma;
}

void set_pixel(uint16_t x, uint16_t y, ws2811_led_t color, ws2811_channel_t *channels, const canvas_t *canvas) {
  debug("Called set_pixel(x: %hu, y: %hu, color: 0x%08x)", x, y, color);
  if (x >= canvas->width || y >= canvas->height)
    errx(EXIT_FAILURE, "Cannot draw outside canvas dimensions");
  uint16_t offset = canvas->topology[(canvas->width * y) + x];
  // Ignore canvas locations that weren't initialized with pixels
  if (offset != USHRT_MAX) {
    // MSB designates which channel to use
    uint8_t channel = offset >> 15;
    // Clear the MSB so we can use pixel as the offset within the channel
    offset &= ~(1 << 15);
    channels[channel].leds[offset] = color;
  }
}

ws2811_led_t get_pixel(uint16_t x, uint16_t y, ws2811_channel_t *channels, const canvas_t *canvas) {
  debug("Called get_pixel(x: %hu, y: %hu)", x, y);
  if (x >= canvas->width || y >= canvas->height)
    errx(EXIT_FAILURE, "Cannot read from outside canvas dimensions");
  uint16_t offset = canvas->topology[(canvas->width * y) + x];
  // Ignore canvas locations that weren't initialized with pixels
  if (offset == USHRT_MAX) {
    // TODO: We should probably store the whole canvas instead of just the
    // actually-mapped pixels in the topology so we don't have to do this...
    // and maybe use OpenGL ES or something to do the low-level drawing.
    return 0x00000000;
  } else {
    // MSB designates which channel to use
    uint8_t channel = offset >> 15;
    // Clear the MSB so we can use pixel as the offset within the channel
    offset &= ~(1 << 15);
   return  channels[channel].leds[offset];
  }
}

void fill(uint16_t x, uint16_t y, uint16_t width, uint16_t height, ws2811_led_t color, ws2811_channel_t *channels, const canvas_t *canvas) {
  debug("Called fill(x: %hu, y: %hu, width: %hu, height: %hu, color: 0x%08x)", x, y, width, height, color);
  if (x >= canvas->width || y >= canvas->height ||
      x + width >= canvas->width || y + height >= canvas->height)
    errx(EXIT_FAILURE, "Cannot draw outside canvas dimensions");
  uint16_t row, col;
  for(row = 0; row < height; row++) {
    for(col = 0; col < width; col++) {
      set_pixel(x + col, y + row, color, channels, canvas);
    }
  }
}

void copy(uint16_t xs, uint16_t ys, uint16_t xd, uint16_t yd, uint16_t width, uint16_t height, bool copy_null, ws2811_channel_t *channels, const canvas_t *canvas) {
  debug("Called copy%s(xs: %hu, ys: %hu, xd: %hu, yd: %hu, width: %hu, height: %hu)", copy_null ? "" : "_blit", xs, ys, xd, yd, width, height);
  // Note: We skip bounds-checking here because it's already done in each call to get_pixel and set_pixel.
  uint16_t row, col;
  ws2811_led_t *buffer = calloc(width * height, sizeof(ws2811_led_t));;
  for(row = 0; row < height; row++) {
    for(col = 0; col < width; col++) {
      buffer[row * width + col] = get_pixel(xs + col, ys + row, channels, canvas);
    }
  }
  // We have to copy to a temporary buffer and then back so that the copy happens "all at once."
  ws2811_led_t color;
  for(row = 0; row < height; row++) {
    for(col = 0; col < width; col++) {
      color = buffer[row * width + col];
      if (color != 0x00000000 || copy_null)
        set_pixel(xd + col, yd + row, color, channels, canvas);
    }
  }
}

void blit(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint8_t *data, ws2811_channel_t *channels, const canvas_t *canvas) {
  debug("Called blit(x: %hu, y: %hu, width: %hu, height: %hu, data: <binary>)", x, y, width, height);
  uint16_t row, col;
  ws2811_led_t color;
  for(row = 0; row < height; row++) {
    for(col = 0; col < width; col++, data += 4) {
      // ws2811_led_t is uint32_t: 0xWWRRGGBB
      // so data should look like [0xWW, 0xRR, 0xGG, 0xBB]
      color = data[0] << 24 | data[1] << 16 | data[2] << 8 | data[3];
      // Ignore totally black pixels in the source image to allow simple sprite masking.
      if (color != 0x00000000)
        set_pixel(x + col, y + row, color, channels, canvas);
    }
  }
}

int main(int argc, char *argv[]) {
  if (argc != 7 && argc != 4)
    errx(EXIT_FAILURE, "Usage: %s <Channel 1 Pin> <Channel 1 Count> <Channel 1 Type> [<Channel 2 Pin> <Channel 2 Count> <Channel 2 Type>]", argv[0]);

  uint8_t gpio_pin1 = atoi(argv[1]);
  uint32_t led_count1 = strtol(argv[2], NULL, 10);
  int strip_type1 = parse_strip_type(argv[3]);

  uint8_t gpio_pin2 = 0;
  uint32_t led_count2 = 0;
  int strip_type2 = WS2811_STRIP_GBR;
  if (argc == 7) {
    gpio_pin2 = atoi(argv[4]);
    led_count2 = strtol(argv[5], NULL, 10);
    strip_type2 = parse_strip_type(argv[6]);
  }

  /*
  Setup the channels. Raspberry Pi supports 2 PWM channels.
  */
  ws2811_t ledstring = {
    .freq = WS2811_TARGET_FREQ,
    .dmanum = DMA_CHANNEL,
    .channel = {
      [0] = {
        .gpionum = gpio_pin1,
        .count = led_count1,
        .invert = 0,
        .brightness = 255,
        .strip_type = strip_type1,
      },
      [1] = {
        .gpionum = gpio_pin2,
        .count = led_count2,
        .invert = 0,
        .brightness = 255,
        .strip_type = strip_type2,
      },
    },
  };

  ws2811_return_t rc = ws2811_init(&ledstring);
  if (rc != WS2811_SUCCESS)
    errx(EXIT_FAILURE, "ws2811_init failed: %d (%s)", rc, ws2811_get_return_t_str(rc));

  canvas_t canvas = {
    .width = 0,
    .height = 0,
    .topology = NULL,
  };

  char buffer[16];
  for (;;) {
    buffer[0] = '\0';
    if (scanf("%15s", buffer) == 0 || strlen(buffer) == 0) {
      if (feof(stdin)) {
        debug("EOF");
        exit(EXIT_SUCCESS);
      } else {
        errx(EXIT_FAILURE, "read error");
      }
    }

    if (strcasecmp(buffer, "init_canvas") == 0) {
      uint16_t width, height;
      char nl;
      if (scanf("%hu %hu%c", &width, &height, &nl) != 3 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in init_canvas command");
      init_canvas(width, height, &canvas);

    } else if (strcasecmp(buffer, "init_pixels") == 0) {
      uint16_t x, y, count, offset;
      uint8_t channel;
      int8_t dx, dy;
      char nl;
      if (scanf("%hhu %hu %hu %hu %hu %hhi %hhi%c", &channel, &offset, &x, &y, &count, &dx, &dy, &nl) != 8 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in init_pixels command");
      init_pixels(channel, offset, x, y, count, dx, dy, &canvas);

    } else if (strcasecmp(buffer, "set_invert") == 0) {
      uint8_t channel, invert;
      char nl;
      if (scanf("%hhu %hhu%c", &channel, &invert, &nl) != 3 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in set_invert command");
      debug("Called set_invert(channel: %hhu, invert: %hhu)", channel, invert);
      if(channel > 1)
        errx(EXIT_FAILURE, "Channel must be 0 or 1 in set_invert command");
      if(invert > 1)
        errx(EXIT_FAILURE, "Invert must be 0 or 1 in set_invert command");
      ledstring.channel[channel].invert = invert;

    } else if (strcasecmp(buffer, "set_brightness") == 0) {
      uint8_t channel, brightness;
      char nl;
      if (scanf("%hhu %hhu%c", &channel, &brightness, &nl) != 3 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in set_brightness command");
      if(channel > 1)
        errx(EXIT_FAILURE, "Channel must be 0 or 1 in set_brightness command");
      set_brightness(channel, brightness, ledstring.channel);

    } else if (strcasecmp(buffer, "set_gamma") == 0) {
      uint8_t channel;
      uint32_t base64_size = 256 * 4 * 4 / 3; // Each color channel has 256 bytes, scaled by 4/3 for Base64
      char *base64_buffer = malloc(base64_size + 1);
      char format[16], nl;
      sprintf(format, "%%hhu %%%us%%c", base64_size);
      if (scanf(format, &channel, base64_buffer, &nl) != 3 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in set_gamma command");
      int decoded_size;
      uint8_t *data = unbase64(base64_buffer, strlen(base64_buffer), &decoded_size);
      if (decoded_size != 4 * 256)
        errx(EXIT_FAILURE, "Size of gamma table must be 4 * 256 bytes in set_gamma command");
      free(base64_buffer);
      set_gamma(channel, data, ledstring.channel);
      free(data);

    } else if (strcasecmp(buffer, "set_pixel") == 0) {
      uint16_t x, y;
      uint8_t r, g, b, w;
      char nl;
      if (scanf("%hu %hu %hhu %hhu %hhu %hhu%c", &x, &y, &r, &g, &b, &w, &nl) != 7 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in set_pixel command");
      // ws2811_led_t is uint32_t: 0xWWRRGGBB
      ws2811_led_t color = (w << 24) | (r << 16) | (g << 8) | b;
      set_pixel(x, y, color, ledstring.channel, &canvas);

    } else if (strcasecmp(buffer, "get_pixel") == 0) {
      uint16_t x, y;
      char nl;
      if (scanf("%hu %hu%c", &x, &y, &nl) != 3 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in get_pixel command");
      get_pixel(y, x, ledstring.channel, &canvas);

    } else if (strcasecmp(buffer, "fill") == 0) {
      uint16_t x, y, width, height;
      uint8_t r, g, b, w;
      char nl;
      if (scanf("%hu %hu %hu %hu %hhu %hhu %hhu %hhu%c", &x, &y, &width, &height, &r, &g, &b, &w, &nl) != 9 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in set_pixel command");
      // ws2811_led_t is uint32_t: 0xWWRRGGBB
      ws2811_led_t color = (w << 24) | (r << 16) | (g << 8) | b;
      fill(x, y, width, height, color, ledstring.channel, &canvas);

    } else if (strcasecmp(buffer, "copy") == 0) {
      uint16_t xs, ys, xd, yd, w, h;
      char nl;
      if (scanf("%hu %hu %hu %hu %hu %hu%c", &xs, &ys, &xd, &yd, &w, &h, &nl) != 7 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in copy command");
      copy(xs, ys, xd, yd, w, h, true, ledstring.channel, &canvas);

    } else if (strcasecmp(buffer, "blit") == 0) {
      uint16_t x, y, width, height;
      uint32_t base64_size;
      if (scanf("%hu %hu %hu %hu %u ", &x, &y, &width, &height, &base64_size) != 5)
        errx(EXIT_FAILURE, "Argument error in blit command");
      char format[16], nl;
      sprintf(format, "%%%us%%c", base64_size);
      char *base64_buffer = malloc(base64_size + 1);
      if (scanf(format, base64_buffer, &nl) != 2 || nl != '\n')
        errx(EXIT_FAILURE, "Unable to read base64-encoded binary from blit command");
      int decoded_size;
      uint8_t *data = unbase64(base64_buffer, strlen(base64_buffer), &decoded_size);
      if (decoded_size != width * height * 4) // Each pixel should have 4 8-bit color channels
        errx(EXIT_FAILURE, "Size of binary data didn't match the width and height in blit command");
      debug("Base64-encoded blit data: %s", base64_buffer);
      free(base64_buffer);
      blit(x, y, width, height, data, ledstring.channel, &canvas);
      free(data);

    } else if (strcasecmp(buffer, "copy_blit") == 0) {
      uint16_t xs, ys, xd, yd, w, h;
      char nl;
      if (scanf("%hu %hu %hu %hu %hu %hu%c", &xs, &ys, &xd, &yd, &w, &h, &nl) != 7 || nl != '\n')
        errx(EXIT_FAILURE, "Argument error in copy_blit command");
      copy(xs, ys, xd, yd, w, h, false, ledstring.channel, &canvas);

    } else if (strcasecmp(buffer, "render") == 0) {
      ws2811_return_t result = ws2811_render(&ledstring);
      if (result != WS2811_SUCCESS)
        errx(EXIT_FAILURE, "ws2811_render failed: %d (%s)", result, ws2811_get_return_t_str(result));

    } else if (strcasecmp(buffer, "print_topology") == 0) {
      debug("Called print_topology()");
      uint16_t x, y, offset;
      char buffer[1024];
      for(y = 0; y < canvas.height; y++) {
        for(x = 0; x < canvas.width; x++) {
          offset = canvas.topology[(canvas.width * y) + x];
          if (offset == USHRT_MAX)
            debug("  [%hu][%hu]: [  -  ]", x, y);
          else
            debug("  [%hu][%hu]: [%5hu]", x, y, offset);
        }
      }

    } else {
      errx(EXIT_FAILURE, "Unrecognized command: '%s'", buffer);
    }
  }
}
