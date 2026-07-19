/* jconfig.h -- libjpeg-turbo configuration for macOS on arm64. */
/* see jconfig.txt for explanations */

#define JPEG_LIB_VERSION 62
#define LIBJPEG_TURBO_VERSION 1.5.1
#define LIBJPEG_TURBO_VERSION_NUMBER 1005001
#define C_ARITH_CODING_SUPPORTED
#define D_ARITH_CODING_SUPPORTED
#define MEM_SRCDST_SUPPORTED

/*
 * Define BITS_IN_JSAMPLE as either
 *   8   for 8-bit sample values (the usual setting)
 *   12  for 12-bit sample values
 * Only 8 and 12 are legal data precisions for lossy JPEG according to the
 * JPEG standard, and the IJG code does not support anything else!
 * We do not support run-time selection of data precision, sorry.
 */

#define BITS_IN_JSAMPLE  8      /* use 8 or 12 */

#define HAVE_UNSIGNED_CHAR
#define HAVE_UNSIGNED_SHORT
/* #define void char */
/* #define const */
#undef __CHAR_UNSIGNED__
#define HAVE_STDDEF_H
#define HAVE_STDLIB_H
#undef NEED_BSD_STRINGS
#undef NEED_SYS_TYPES_H
#undef NEED_FAR_POINTERS	/* macOS uses a flat memory model */
#undef INCOMPLETE_TYPES_BROKEN

/* Keep the libjpeg boolean ABI used by the bundled sources. */
typedef unsigned char boolean;
#define HAVE_BOOLEAN		/* prevent jmorecfg.h from redefining it */

/* Keep the integer ABI used by the bundled libjpeg sources. */
typedef short INT16;
typedef signed int INT32;
#define XMD_H                   /* prevent jmorecfg.h from redefining it */

#ifdef JPEG_INTERNALS

#undef RIGHT_SHIFT_IS_UNSIGNED

#endif /* JPEG_INTERNALS */

#define SIZEOF_SIZE_T 8
