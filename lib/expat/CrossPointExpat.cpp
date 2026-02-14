#include "CrossPointExpat.h"

#include <assert.h>
#include <limits.h> /* INT_MAX, UINT_MAX */
#include <math.h>   /* isnan */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h> /* SIZE_MAX, uintptr_t */
#include <stdio.h>  /* fprintf */
#include <stdlib.h> /* getenv, rand_s */
#include <string.h> /* memset(), memcpy() */

#include <errno.h>
#include <fcntl.h>     /* O_RDONLY */
#include <sys/time.h>  /* gettimeofday() */
#include <sys/types.h> /* getpid() */
#include <unistd.h>    /* getpid() */

#include "ascii.h"
#include "expat.h"
#include "siphash.h"


namespace Expat {
#include "xmlparse.c"
}
