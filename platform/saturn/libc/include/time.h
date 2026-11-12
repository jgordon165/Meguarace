/* <time.h> for the Saturn build: time counted in vblanks since boot. */
#ifndef SAT_TIME_H
#define SAT_TIME_H
#ifdef __cplusplus
extern "C" {
#endif
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 60
time_t  time(time_t* t);
clock_t clock(void);
#ifdef __cplusplus
}
#endif
#endif
