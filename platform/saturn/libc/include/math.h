/*
 * <math.h> for the Saturn build: libyaul has no maths library. The float
 * functions Lua uses (it's built with 32-bit floats) come from musl
 * (third_party/musl/src/math), with the few double ones they need.
 */
#ifndef SAT_MATH_H
#define SAT_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef float  float_t;
typedef double double_t;

#define NAN       __builtin_nanf("")
#define INFINITY  __builtin_inff()
#define HUGE_VALF INFINITY
#define HUGE_VAL  __builtin_inf()

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4

#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)
#define isnan(x)      __builtin_isnan(x)
#define isinf(x)      __builtin_isinf(x)
#define isfinite(x)   __builtin_isfinite(x)
#define isnormal(x)   __builtin_isnormal(x)
#define signbit(x)    __builtin_signbit(x)

#define M_PI    3.14159265358979323846
#define M_PI_2  1.57079632679489661923

float floorf(float);
float ceilf(float);
float fabsf(float);
float fmodf(float, float);
float sqrtf(float);
float powf(float, float);
float expf(float);
float logf(float);
float log2f(float);
float log10f(float);
float sinf(float);
float cosf(float);
float tanf(float);
float asinf(float);
float acosf(float);
float atanf(float);
float atan2f(float, float);
float frexpf(float, int*);
float ldexpf(float, int);
float scalbnf(float, int);

double floor(double);
double scalbn(double, int);
double fabs(double);
double sqrt(double);

#ifdef __cplusplus
}
#endif

#endif
