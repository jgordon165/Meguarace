/* <setjmp.h> for the Saturn build (Lua's error handling): setjmp.S. */
#ifndef SAT_SETJMP_H
#define SAT_SETJMP_H
#ifdef __cplusplus
extern "C" {
#endif
/* r8-r15 and pr: what the SH-2 ABI asks a callee to keep. */
typedef unsigned long jmp_buf[9];
int  setjmp(jmp_buf env) __attribute__((__returns_twice__));
void longjmp(jmp_buf env, int value) __attribute__((__noreturn__));
#ifdef __cplusplus
}
#endif
#endif
