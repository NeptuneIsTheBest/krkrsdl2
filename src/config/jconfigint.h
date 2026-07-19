#define VERSION "1.5.1"
#define BUILD "20160817"
#define PACKAGE_NAME "libjpeg-turbo"

#ifndef INLINE
#if defined(__GNUC__)
#define INLINE inline __attribute__((always_inline))
#else
#define INLINE
#endif
#endif
