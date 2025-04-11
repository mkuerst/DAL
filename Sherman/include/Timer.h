#if !defined(_TIMER_H_)
#define _TIMER_H_

#include <cstdint>
#include <cstdio>
#include <time.h>
#include <cpuid.h>

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
        
inline double estimate_tsc_freq_fallback() {
  struct timespec start_ts, end_ts;
  unsigned aux;
  uint64_t start_cycles = __rdtscp(&aux);
  clock_gettime(CLOCK_MONOTONIC, &start_ts);

  struct timespec sleep_time = {.tv_sec = 0, .tv_nsec = 100000000}; // 100ms
  nanosleep(&sleep_time, NULL);

  uint64_t end_cycles = __rdtscp(&aux);
  clock_gettime(CLOCK_MONOTONIC, &end_ts);

  uint64_t elapsed_cycles = end_cycles - start_cycles;
  uint64_t elapsed_ns = (end_ts.tv_sec - start_ts.tv_sec) * 1e9 +
                        (end_ts.tv_nsec - start_ts.tv_nsec);

  return (double)elapsed_cycles * 1e9 / elapsed_ns;
}

inline double get_tsc_freq_hz() {
  unsigned int eax, ebx, ecx, edx;
  if (__get_cpuid_count(0x15, 0, &eax, &ebx, &ecx, &edx)) {
      if (eax != 0 && ebx != 0) {
          double tsc_freq = (double)ecx * ((double)ebx / (double)eax);
          if (tsc_freq > 0)
              return tsc_freq;
      }
  }

  return estimate_tsc_freq_fallback();
}

class Timer {
public:
  Timer() {
    tsc_freq = get_tsc_freq_hz();
  };
  double tsc_freq;

  #ifdef CYCLE_TIME
  void begin() { start = rdtscp(); }

  // uint64_t end(uint64_t loop = 1) {
  //   return (rdtscp() - start) * 1e9 / tsc_freq;
  // }
  uint64_t end(uint64_t loop = 1) {
    double elapsed_cycles = static_cast<double>(rdtscp() - start);
    double ns_total = (elapsed_cycles / tsc_freq) * 1e9;
    // std::cerr << "tsc_freq: " << tsc_freq << endl;
    // std::cerr << "elapsed_cycles: " << elapsed_cycles << endl;
    // std::cerr << "ns_total: " << ns_total << endl;
    return static_cast<uint64_t>(ns_total / loop + 0.5);  // rounding
  }
  #else
  void begin() { clock_gettime(CLOCK_REALTIME, &s); }

  uint64_t end(uint64_t loop = 1) { 
    asm volatile("" ::: "memory");
    this->loop = loop;
    clock_gettime(CLOCK_REALTIME, &e);
    uint64_t ns_all =
        (e.tv_sec - s.tv_sec) * 1000000000ull + (e.tv_nsec - s.tv_nsec);
    ns = ns_all / loop;

    return ns;
  }
  #endif

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
