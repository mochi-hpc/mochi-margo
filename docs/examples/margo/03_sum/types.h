#ifndef PARAM_H
#define PARAM_H

#include <mercury.h>
#include <mercury_macros.h>

/* We use the Mercury macros to define the input
 * and output structures along with the serialization
 * functions.
 */
MERCURY_GEN_PROC(sum_in_t,
        ((int32_t)(x))\
        ((int32_t)(y)))

MERCURY_GEN_PROC(sum_out_t, ((int32_t)(ret)))

#endif
