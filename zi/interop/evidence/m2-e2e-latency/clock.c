#include <stdio.h>
#include <time.h>
#include <stdint.h>
static int64_t ns(struct timespec t) { return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec; }
int main(void) {
  const unsigned count = 1000000;
  puts("round,clock,calls,ns_per_call");
  for (unsigned r = 0; r < 7; ++r) {
    struct timespec begin, end, value;
    clock_gettime(CLOCK_MONOTONIC, &begin);
    for (unsigned i = 0; i < count; ++i) clock_gettime(CLOCK_REALTIME, &value);
    clock_gettime(CLOCK_MONOTONIC, &end);
    printf("%u,realtime,%u,%.3f\n", r, count, (double)(ns(end)-ns(begin))/count);
  }
}
