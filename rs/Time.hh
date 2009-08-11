/**
 * @brief Uzyteczne funkcje zwiazane z pomiarem czasu.
 * @author Piotr Truszkowski
 */

#ifndef __RS_TIME_HH__
#define __RS_TIME_HH__

#include <stdint.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/time.h>

#include <rs/Exception.hh>

class Time {
  public:
    static const size_t stamp_length = 19;

    static timeval msec2timeval(int msec) throw()
    {
      timeval tv;
      tv.tv_sec = msec/1000;
      tv.tv_usec = (msec%1000)*1000;
      return tv;
    }

    static uint32_t in_sec(void) throw()
    {
      timeval tv;
      gettimeofday(&tv, 0);
      return tv.tv_sec;
    }

    static uint64_t in_msec(void) throw()
    {
      timeval tv;
      gettimeofday(&tv, 0);
      return ((uint64_t)tv.tv_sec)*1000ULL + ((uint64_t)tv.tv_usec)/1000ULL;
    }

    static uint64_t in_usec(void) throw()
    {
      timeval tv;
      gettimeofday(&tv, 0);
      return ((uint64_t)tv.tv_sec)*1000000ULL + ((uint64_t)tv.tv_usec);
    }

    static const char *stamp(void) throw()
    {
      static char buf[stamp_length+1];
      return stamp(buf);
    }

    static const char *stamp(char *buf) throw()
    {
      time_t t = time(0);
      struct tm lt;
      if (localtime_r(&t, &lt) == NULL) 
        throw EInternal("localtime");
      
      int sn = snprintf(buf, stamp_length+1, "%4d-%.2d-%.2d %.2d:%.2d:%.2d",
          lt.tm_year+1900, lt.tm_mon+1, lt.tm_mday, 
          lt.tm_hour, lt.tm_min, lt.tm_sec);

      if (sn != (int)stamp_length) throw EInternal("snprintf");

      return buf;
    }

    static uint32_t stamp2secs(const char *stamp) throw()
    { 
      tm lt;
      char *c = NULL;
      
      // "YYYY-" Liczba lat po 1900roku
      lt.tm_year  = strtol(stamp +  0, &c, 10) - 1900; 
      if (c != stamp +  4 || (*c) != '-') return -1;
      
      // "MM-" Liczba miesiecy 0-11
      lt.tm_mon   = strtol(stamp +  5, &c, 10) - 1;
      if (c != stamp +  7 || (*c) != '-' || lt.tm_mon < 0 || lt.tm_mon >= 12) return -1;

      // "DD " Liczba dni 1-31
      lt.tm_mday  = strtol(stamp +  8, &c, 10);
      if (c != stamp + 10 || (*c) != ' ' || lt.tm_mday <= 0 || lt.tm_mday > 31) return -1;

      // "hh:" Liczba godzin 0-23
      lt.tm_hour  = strtol(stamp + 11, &c, 10);
      if (c != stamp + 13 || (*c) != ':' || lt.tm_hour < 0 || lt.tm_hour >= 24) return -1;

      // "mm:" Liczba minut 0-59
      lt.tm_min   = strtol(stamp + 14, &c, 10);
      if (c != stamp + 16 || (*c) != ':' || lt.tm_min < 0 || lt.tm_min >= 60) return -1;

      // "ss" Liczba sekund 0-59
      if (stamp[17] < '0' || stamp[17] > '9' || stamp[18] < '0' || stamp[18] > '9') return -1;
      lt.tm_sec   = (stamp[17]-'0')*10 + (stamp[17]-'0'); // "ss"
      if (lt.tm_sec < 0 || lt.tm_sec >= 60) return -1;
      
      lt.tm_isdst = -1;
      
      return (uint32_t)mktime(&lt);
    }

  private:
    /**
     * - !! - Nie tworzymy obiektu - !! -
     */

    Time(void) { }
    Time(const Time &) { }
    ~Time(void) { }
};

#endif
