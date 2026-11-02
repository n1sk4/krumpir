#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "writer.h"

int main(void)
{
  char path[] = "/tmp/krumpir_test_writer_XXXXXX";
  int fd = mkstemp(path);
  assert(fd >= 0);
  close(fd);

  ring_t ring;
  assert(ring_init(&ring, 16) == 0);
  const char *in_names[] = {"u"};
  const char *out_names[] = {"y", "z"};
  writer_t *w = writer_start(&ring, path, NULL, in_names, 1, out_names, 2, NULL, NULL);
  assert(w != NULL);

  for (uint64_t i = 0; i < 3; i++) {
    telem_rec_t rec = {0};
    rec.n = i;
    rec.t = (double)i * 0.5;
    rec.in[0] = 1.5;
    rec.out[0] = 0.25;
    rec.out[1] = -2.0;
    rec.wake_ns = 1000 + (int64_t)i;
    rec.step_ns = 52000;
    rec.flags = i == 2 ? TELEM_FLAG_OVERRUN : 0;
    assert(ring_push(&ring, &rec));
  }
  assert(writer_stop(w) == 3);

  FILE *fp = fopen(path, "r");
  assert(fp != NULL);
  char line[256];
  assert(fgets(line, sizeof line, fp));
  assert(strcmp(line, "n,t,u,y,z,wake_ns,step_ns,flags\n") == 0);
  assert(fgets(line, sizeof line, fp));
  assert(strcmp(line, "0,0,1.5,0.25,-2,1000,52000,0\n") == 0);
  assert(fgets(line, sizeof line, fp));
  assert(strcmp(line, "1,0.5,1.5,0.25,-2,1001,52000,0\n") == 0);
  assert(fgets(line, sizeof line, fp));
  assert(strcmp(line, "2,1,1.5,0.25,-2,1002,52000,1\n") == 0);
  assert(!fgets(line, sizeof line, fp));
  fclose(fp);

  /* Continuing: the rows go after the old ones, without a second header, n and t offset */
  const writer_cont_t cont = {.n = 3, .t = 1.5};
  w = writer_start(&ring, path, &cont, in_names, 1, out_names, 2, NULL, NULL);
  assert(w != NULL);
  telem_rec_t again = {.n = 0, .t = 0.5, .in = {7}};
  assert(ring_push(&ring, &again));
  assert(writer_stop(w) == 1);
  fp = fopen(path, "r");
  int lines = 0, headers = 0;
  while (fgets(line, sizeof line, fp)) {
    lines++;
    headers += strncmp(line, "n,t,", 4) == 0;
  }
  fclose(fp);
  assert(lines == 5 && headers == 1);
  assert(strcmp(line, "3,2,7,0,0,0,0,0\n") == 0); /* the 4th row, at 1.5 + 0.5 s */

  /* Continuing a file that does not exist yet writes the header */
  remove(path);
  w = writer_start(&ring, path, &cont, in_names, 1, out_names, 2, NULL, NULL);
  assert(w != NULL && writer_stop(w) == 0);
  fp = fopen(path, "r");
  assert(fgets(line, sizeof line, fp) && strcmp(line, "n,t,u,y,z,wake_ns,step_ns,flags\n") == 0);
  fclose(fp);

  /* Unwritable path is reported, not crashed on. */
  assert(writer_start(&ring, "/nonexistent-dir/x.csv", NULL, in_names, 1, out_names, 2, NULL,
                      NULL) == NULL);

  remove(path);
  ring_free(&ring);
  puts("test_writer: ok");
  return 0;
}
