#pragma once

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;
typedef uint64_t           uintptr_t;
typedef uint64_t           size_t;
typedef int64_t            ssize_t;
/* bool — C11: _Bool is built-in, 'bool' is stdbool.h macro (we skip stdbool.h).
   C23: bool is a keyword. Guard with __bool_true_false_are_defined. */
#ifndef __bool_true_false_are_defined
#  ifndef bool
#    define bool  _Bool
#  endif
#  define true  ((_Bool)1)
#  define false ((_Bool)0)
#  define __bool_true_false_are_defined 1
#endif

#define NULL  ((void*)0)

#define ALIGN_UP(x, a)   (((x) + ((a)-1)) & ~((a)-1))
#define ALIGN_DOWN(x, a) ((x) & ~((a)-1))
#define ARRAY_LEN(a)     (sizeof(a) / sizeof((a)[0]))

#define PAGE_SIZE 4096ULL
#define PAGE_MASK (PAGE_SIZE - 1)

/* Compiler hints */
#define PACKED      __attribute__((packed))
#define NORETURN    __attribute__((noreturn))
#define UNUSED      __attribute__((unused))
#define ALWAYS_INLINE __attribute__((always_inline)) inline
