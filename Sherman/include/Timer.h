#if !defined(_TIMER_H_)
#define _TIMER_H_

#include <cstdint>
#include <cstdio>
#include <time.h>

#define CYCLES_PER_US 3L

#include <cstdint>

inline uint64_t rdtscp() {
    unsigned int lo, hi;
    asm volatile (
        "rdtscp"
        : "=a"(lo), "=d"(hi)
        :: "rcx");
    return (static_cast<uint64_t>(hi) << 32) | lo;
}

class Timer {
public:
  Timer() = default;

  void begin() { clock_gettime(CLOCK_REALTIME, &s); }
  // void begin() { start = rdtscp(); }

  uint64_t end(uint64_t loop = 1) { 
    asm volatile("" ::: "memory");
    this->loop = loop;
    clock_gettime(CLOCK_REALTIME, &e);
    uint64_t ns_all =
        (e.tv_sec - s.tv_sec) * 1000000000ull + (e.tv_nsec - s.tv_nsec);
    ns = ns_all / loop;

    return ns;
  }
  // uint64_t end(uint64_t loop = 1) {
  //   return (rdtscp() - start) / CYCLES_PER_US;
  // }

  void print() {

    if (ns < 1000) {
      printf("%ldns per loop\n", ns);
    } else {
      printf("%lfus per loop\n", ns * 1.0 / 1000);
    }
  }

  static uint64_t get_time_ns() {
    timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return 1000000000ull * now.tv_sec + now.tv_nsec;
  }

  static void sleep(uint64_t sleep_ns) {
    Timer clock;

    clock.begin();
    while (true) {
      if (clock.end() >= sleep_ns) {
        return;
      }
    }
  }

  void end_print(uint64_t loop = 1) {
    end(loop);
    print();
  }

private:
  uint64_t start;
  timespec s, e;
  uint64_t loop;
  uint64_t ns;
};

#endif // _TIMER_H_
