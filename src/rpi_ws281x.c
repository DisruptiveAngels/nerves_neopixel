#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>
#include <strings.h>

#include "rpi_ws281x/clk.h"
#include "rpi_ws281x/gpio.h"
#include "rpi_ws281x/dma.h"
#include "rpi_ws281x/pwm.h"
#include "rpi_ws281x/ws2811.h"

#include "erlcmd.h"
#include "utils.h"

#define DMA_CHANNEL 5


/*
   Receive from Erlang a list of tuples
   {channel, {brightness, data}}
   Each tuple contains
   {brightness, led_data}
   */
static void led_handle_request(const char *req, void *cookie) {

  ws2811_t *ledstring = (ws2811_t *)cookie;

  int req_index = sizeof(uint16_t);
  if (ei_decode_version(req, &req_index, NULL) < 0)
    errx(EXIT_FAILURE, "Message version issue?");

  int arity;
  if (ei_decode_tuple_header(req, &req_index, &arity) < 0 ||
      arity != 2)
    errx(EXIT_FAILURE, "expecting {{channel1}, {channel2}} tuple");


  unsigned int ch_num;
  ei_decode_long(req, &req_index, (long int *) &ch_num);

  ws2811_channel_t *channel = &ledstring->channel[ch_num];

  if (ei_decode_tuple_header(req, &req_index, &arity) < 0 ||
      arity != 2)
    errx(EXIT_FAILURE, "expecting {brightness, led_data} tuple");

  unsigned int brightness;
  if (ei_decode_long(req, &req_index, (long int *) &brightness) < 0 ||
      brightness > 255)
    errx(EXIT_FAILURE, "brightness: min=0, max=255");

  channel->brightness = brightness;

  long int led_data_len = (4 * channel->count);
  ei_decode_binary(req, &req_index, channel->leds, &led_data_len);

  ws2811_return_t rc = ws2811_render(ledstring);
  if (rc != WS2811_SUCCESS)
    errx(EXIT_FAILURE, "ws2811_render failed: %d (%s)", rc, ws2811_get_return_t_str(rc));
}

int parse_strip_type(char *strip_type) {
  if (!strncasecmp("rgb", strip_type, 4)) {
    return WS2811_STRIP_RGB;
  }
  else if (!strncasecmp("rbg", strip_type, 4)) {
    return WS2811_STRIP_RBG;
  }
  else if (!strncasecmp("grb", strip_type, 4)) {
    return WS2811_STRIP_GRB;
  }
  else if (!strncasecmp("gbr", strip_type, 4)) {
    return WS2811_STRIP_GBR;
  }
  else if (!strncasecmp("brg", strip_type, 4)) {
    return WS2811_STRIP_BRG;
  }
  else if (!strncasecmp("bgr", strip_type, 4)) {
    return WS2811_STRIP_BGR;
  }
  else if (!strncasecmp("rgbw", strip_type, 4)) {
    return SK6812_STRIP_RGBW;
  }
  else if (!strncasecmp("rbgw", strip_type, 4)) {
    return SK6812_STRIP_RBGW;
  }
  else if (!strncasecmp("grbw", strip_type, 4)) {
    return SK6812_STRIP_GRBW;
  }
  else if (!strncasecmp("gbrw", strip_type, 4)) {
    return SK6812_STRIP_GBRW;
  }
  else if (!strncasecmp("brgw", strip_type, 4)) {
    return SK6812_STRIP_BRGW;
  }
  else if (!strncasecmp("bgrw", strip_type, 4)) {
    return SK6812_STRIP_BGRW;
  }
  else {
    fprintf (stderr, "ERROR: Invalid strip type %s\n", strip_type);
    exit (EXIT_FAILURE);
  }
}

int main(int argc, char *argv[]) {
  if (argc != 7) {
    fprintf(stderr, "Usage: %s <Ch 1 GPIO Pin> <Ch 1 LED Count> <Ch 1 strip type> <Ch 2 GPIO Pin> <Ch 2 LED Count> <Ch 2 strip type>\n", argv[0]);
    fprintf(stderr, "Valid options for GPIO pins vary by PWM channel and Raspberry Pi model:\n");
    fprintf(stderr, "  PWM Ch 1: 12, 18, 40, 52\n");
    fprintf(stderr, "  PWM Ch 2: 13, 19, 41, 45, 53\n");
    fprintf(stderr, "Valid options for Strip Type:\n");
    fprintf(stderr, "  rgb, rbg, grb, gbr, brg, bgr,\n");
    fprintf(stderr, "  rgbw, rbgw, grbw, gbrw, brgw, bgrw,\n");
    exit(EXIT_FAILURE);
  }

  uint8_t gpio_pin1 = atoi(argv[1]);
  uint32_t led_count1 = strtol(argv[2], NULL, 10);
  int strip_type1 = parse_strip_type(argv[3]);

  uint8_t gpio_pin2 = atoi(argv[4]);
  uint32_t led_count2 = strtol(argv[5], NULL, 10);
  int strip_type2 = parse_strip_type(argv[6]);

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
        .brightness = 0,
        .strip_type = strip_type1,
      },
      [1] = {
        .gpionum = gpio_pin2,
        .count = led_count2,
        .invert = 0,
        .brightness = 0,
        .strip_type = strip_type2,
      },
    },
  };

  ws2811_return_t rc = ws2811_init(&ledstring);
  if (rc != WS2811_SUCCESS)
    errx(EXIT_FAILURE, "ws2811_init failed: %d (%s)", rc, ws2811_get_return_t_str(rc));

  struct erlcmd handler;
  erlcmd_init(&handler, led_handle_request, &ledstring);

  for (;;) {
    erlcmd_process(&handler);
  }
}
