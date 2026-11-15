/*
 * Included first in every Saturn compile unit (-include): points the
 * standard names Lua uses at what the Saturn build has.
 *   snprintf -> third_party/printf (libyaul's has no floating point)
 *   Lua's print and error output -> the screen and log (sat_lua.c)
 */
#ifndef SAT_LUA_PRELUDE_H
#define SAT_LUA_PRELUDE_H
#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdio.h>

int snprintf_(char* s, size_t count, const char* format, ...);
int vsnprintf_(char* s, size_t count, const char* format, __builtin_va_list arg);
#define snprintf  snprintf_
#define vsnprintf vsnprintf_

double strtod(const char* restrict s, char** restrict end);
float  strtof(const char* restrict s, char** restrict end);
char*  getenv(const char* name);
int    strcoll(const char* a, const char* b);
int    abs(int v);
long   labs(long v);

void sat_lua_write(const char* s, size_t n);
#define lua_writestring(s, l)          sat_lua_write((s), (l))
#define lua_writeline()                sat_lua_write("\n", 1)
#define lua_writestringerror(s, p)     sat_lua_writeerr((s), (p))
void sat_lua_writeerr(const char* fmt, const char* p);

#endif /* !__ASSEMBLER__ */
#endif
