#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <setjmp.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <strings.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#include "platform/platform.inc"

#define SOLAN_VERSION "1.0.0"
#define FRAMES_MAX 4000
#define STACK_MAX (1 << 18)
#define NEST_MAX 150
