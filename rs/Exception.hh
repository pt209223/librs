/**
 * @brief Hierarchia wyjatkow
 * @author Piotr Truszkowski
 */

#ifndef __RS_EXCEPTION_HH__
#define __RS_EXCEPTION_HH__

#include <exception>
#include <cstdlib>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <stdarg.h>

class Exception : public std::exception {
	public:
		Exception(void) throw() { }
		const char *what(void) const throw() { return "Exception"; }
};

#define DEF_EXC(name, up)                       \
  class name : public up {                      \
    public:                                     \
      name(void) throw() { }                    \
                                                \
      const char *what(void) const throw()      \
      {                                         \
        return #name;                           \
      }                                         \
  }

#define DEF_EXC_WITH_DESCR(name, up)            \
  class name : public up {                      \
    public:                                     \
      name(void) throw()                        \
      {                                         \
        snprintf(exc, maxexclen, #name);        \
      }                                         \
                                                \
      name(const char *fmt, ...) throw()        \
      {                                         \
        va_list args;                           \
        va_start(args, fmt);                    \
        vsnprintf(exc, maxexclen, fmt, args);   \
        va_end(args);                           \
      }                                         \
                                                \
      const char *what(void) const throw()      \
      {                                         \
        return exc;                             \
      }                                         \
                                                \
    private:                                    \
      static const size_t maxexclen = 256;      \
      char exc[maxexclen];                      \
  }


DEF_EXC_WITH_DESCR(EInternal, Exception);
DEF_EXC_WITH_DESCR(EExternal, Exception);

DEF_EXC(EInvalid   ,  Exception);
DEF_EXC(EAlready   ,  Exception);
DEF_EXC(ENotFound  ,  Exception);
DEF_EXC(ENoAccess  ,  Exception);
DEF_EXC(EAgain     ,  Exception);
DEF_EXC(EResourses ,  Exception);
DEF_EXC(ENoMemory  ,  EResourses);
DEF_EXC(ENoSpace   ,  EResourses);
DEF_EXC(EBusy      ,  EResourses);
DEF_EXC(ELimit     ,  EResourses);
DEF_EXC(ETimeout   ,  EResourses);

#endif

