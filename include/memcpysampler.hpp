#pragma once
#ifndef MEMCPYSAMPLER_HPP
#define MEMCPYSAMPLER_HPP

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h> // for getpid()
#include <signal.h>

#if defined(__x86_64__)
#include "rtememcpy.h"
#endif
#include "samplefile.hpp"


template <uint64_t MemcpySamplingRateBytes>
class MemcpySampler {
  enum { MemcpySignal = SIGPROF };
  static constexpr auto flags = O_WRONLY | O_CREAT | O_SYNC | O_APPEND; // O_TRUNC;
  static constexpr auto perms = S_IRUSR | S_IWUSR;
  static constexpr auto fname = "/tmp/scalene-memcpy-signalXXXXX";
public:
  MemcpySampler()
    : _samplefile((char*) "/tmp/scalene-memcpy-signal@", (char*) "/tmp/scalene-memcpy-lock@"),
      _interval (MemcpySamplingRateBytes),
      _memcpyOps (0),
      _memcpyTriggered (0)
  {
    signal(MemcpySignal, SIG_IGN);
  }

  int local_strlen(const char * str) {
    int len = 0;
    while (*str != '\0') {
      len++;
      str++;
    }
    return len;
  }

  ATTRIBUTE_ALWAYS_INLINE inline void * memcpy(void * dst, const void * src, size_t n) {
    auto result = local_memcpy(dst, src, n);
    incrementMemoryOps(n);
    return result; // always dst
  }

  ATTRIBUTE_ALWAYS_INLINE inline void * memmove(void * dst, const void * src, size_t n) {
    auto result = local_memmove(dst, src, n);
    incrementMemoryOps(n);
    return result; // always dst
  }

  ATTRIBUTE_ALWAYS_INLINE inline char * strcpy(char * dst, const char * src) {
    auto n = ::strlen(src);
    auto result = local_strcpy(dst, src);
    incrementMemoryOps(n);
    return result; // always dst
  }
  
private:

  //// local implementations of memcpy and friends.
  Sampler<MemcpySamplingRateBytes> _memcpySampler;
  SampleFile _samplefile;
  ATTRIBUTE_ALWAYS_INLINE inline void * local_memcpy(void * dst, const void * src, size_t n) {
#if defined(__APPLE__)
    return ::memcpy(dst, src, n);
#else
    return rte_memcpy(dst, src, n);
#endif
  }
  
  void * local_memmove(void * dst, const void * src, size_t n) {
#if defined(__APPLE__)
    return ::memmove(dst, src, n);
#else
    // TODO: optimize if these areas don't overlap.
    void * buf = malloc(n);
    local_memcpy(buf, src, n);
    local_memcpy(dst, buf, n);
    free(buf);
    return dst;
#endif
  }

  char * local_strcpy(char * dst, const char * src) {
    char * orig_dst = dst;
    while (*src != '\0') {
      *dst++ = *src++;
    }
    *dst = '\0';
    return orig_dst;
  }
  
  void incrementMemoryOps(int n) {
    _memcpyOps += n;
    auto sampleMemop = _memcpySampler.sample(n);
    if (unlikely(sampleMemop)) {
      writeCount();
      _memcpyTriggered++;
      _memcpyOps = 0;
#if !SCALENE_DISABLE_SIGNALS
      raise(MemcpySignal);
#endif
    }
  }
  
  uint64_t _memcpyOps;
  unsigned long long _memcpyTriggered;
  uint64_t _interval;

  void writeCount() {
    char buf[255];

    stprintf::stprintf(buf, "@,@\n\n", _memcpyTriggered, _memcpyOps);
    _samplefile.writeToFile(buf);
  }
};


#endif
