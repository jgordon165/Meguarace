/* <endian.h> for the Saturn build (musl's maths reads it): the SH-2 is big-endian. */
#ifndef SAT_ENDIAN_H
#define SAT_ENDIAN_H
#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN    4321
#define __BYTE_ORDER    __BIG_ENDIAN
#endif
