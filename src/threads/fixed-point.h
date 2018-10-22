#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

/* As mentioned in Pintos manual, we need to break the bits. 
   Also, we used an example of p.q numbers*/
/* This is the integer part. */
#define P 17
/* This is the fractional part. */
#define Q 14
/* We also need to handle fractions. */ 
#define FRAC 1 << (Q)

/* Math functions are defined as below, as per the guidelines 
   of the Pintos manual. */

#define CONVERT_TO_FIXED_POINT(n) (n) * (FRAC)
#define CONVERSION_TO_INTEGER_ZERO_ROUNDING(x) (x) / (FRAC)
/* Using tenary operators for nearest rounding. */
#define CONVERSION_TO_INTEGER_NEAREST_ROUNDING(x) ((x) >= 0 ? ((x) + (FRAC) / 2) / (FRAC) : ((x) - (FRAC) / 2) / (FRAC))
#define ADD_TWO_FIXED_POINT_NUMBERS(x, y) (x) + (y)
#define SUBTRACT_TWO_FIXED_POINT_NUMBERS(x, y) (x) - (y)
#define ADD_FP_AND_INT(x, n) (x) + (n) * (FRAC)
#define SUBTRACT_FP_AND_INT(x, n) (x) - (n) * (FRAC)
#define MULTIPLY_FIXED_POINT_NUMBERS(x, y) ((int64_t)(x)) * (y) / (FRAC)
#define MULTIPLY_FP_AND_INT(x, n) (x) * (n)
#define DIVIDE_FIXED_POINT_NUMBERS(x, y) ((int64_t)(x)) * (FRAC) / (y)
#define DIVIDE_FP_AND_INT(x, n) (x) / (n)

#endif
