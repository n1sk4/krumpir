#include "timing_pin.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int timing_pin_open(int line, char *err, size_t errlen)
{
  const int chip = open(TIMING_PIN_CHIP, O_RDWR | O_CLOEXEC);
  if (chip < 0) {
    snprintf(err, errlen, "rt.timing_gpio: cannot open %s: %s", TIMING_PIN_CHIP, strerror(errno));
    return -1;
  }
  struct gpio_v2_line_request req;
  memset(&req, 0, sizeof req);
  req.offsets[0] = (unsigned)line;
  req.num_lines = 1;
  req.config.flags = GPIO_V2_LINE_FLAG_OUTPUT;
  snprintf(req.consumer, sizeof req.consumer, "krumpir tick");
  const int rc = ioctl(chip, GPIO_V2_GET_LINE_IOCTL, &req);
  const int e = errno;
  close(chip);
  if (rc < 0) {
    snprintf(err, errlen, "rt.timing_gpio: cannot use GPIO %d on %s: %s", line, TIMING_PIN_CHIP,
             strerror(e));
    return -1;
  }
  return req.fd;
}

void timing_pin_set(int fd, bool high)
{
  struct gpio_v2_line_values v = {.bits = high ? 1u : 0u, .mask = 1u};
  ioctl(fd, GPIO_V2_LINE_SET_VALUES_IOCTL, &v);
}

void timing_pin_close(int fd)
{
  if (fd > 0) {
    timing_pin_set(fd, false);
    close(fd);
  }
}
