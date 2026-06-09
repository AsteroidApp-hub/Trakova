/* Portable config.h for embedding libmp3lame as a static library.
 * Derived from configMS.h, applies to macOS / Linux / Windows.
 */
#ifndef LAME_CONFIG_H
#define LAME_CONFIG_H

#define PACKAGE "lame"
#define VERSION "3.99.5"
#define PACKAGE_NAME "lame"
#define PACKAGE_TARNAME "lame"
#define PACKAGE_VERSION "3.99.5"
#define PACKAGE_STRING "lame 3.99.5"

/* ANSI C / POSIX */
#define STDC_HEADERS 1
#define PROTOTYPES 1
#define USE_FAST_LOG 1

/* Sizes (assume 64-bit LP64 / LLP64 mixed; long is 4 on Win, 8 on Unix.
 * We don't actually use SIZEOF_LONG in libmp3lame source paths we need.) */
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_UNSIGNED_INT 4
#define SIZEOF_UNSIGNED_SHORT 2

#if defined(_WIN32) && !defined(__CYGWIN__)
  #define SIZEOF_LONG 4
  #define SIZEOF_UNSIGNED_LONG 4
  #define SIZEOF_LONG_DOUBLE 8
#else
  #define SIZEOF_LONG 8
  #define SIZEOF_UNSIGNED_LONG 8
  #define SIZEOF_LONG_DOUBLE 16
#endif

/* Standard headers we can rely on across modern toolchains */
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_MEMORY_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1

/* Integer types */
#define HAVE_INT8_T 1
#define HAVE_INT16_T 1
#define HAVE_INT32_T 1
#define HAVE_INT64_T 1
#define A_INT32_T int32_t
#define A_INT64_T int64_t

/* Math: <sys/ieee754.h> provides ieee754_float32_t on some Linux distros only.
 * On macOS / Windows we provide the typedef ourselves. */
#ifndef __SYS_IEEE754_H
typedef float  ieee754_float32_t;
typedef double ieee754_float64_t;
#endif

/* Disable the optional decoder path inside libmp3lame; we only encode.
 * NOTE: a few files reference HAVE_MPGLIB, but the mpglib sources are still
 *       compiled to satisfy linker dependencies in places that #include them. */
/* #define HAVE_MPGLIB 1 */
/* #define DECODE_ON_THE_FLY 1 */

/* No NASM assembly — pure C build for portability */
/* #define HAVE_NASM 1 */

/* Symbol visibility / inlining */
#if defined(_MSC_VER)
  #define inline __inline
  #define LAME_HAVE_STRTOL 1
  #pragma warning(disable: 4244 4267 4305 4554 4018 4101 4146 4133)
#endif

#ifndef LAME_LIBRARY_BUILD
#define LAME_LIBRARY_BUILD 1
#endif

#endif /* LAME_CONFIG_H */
