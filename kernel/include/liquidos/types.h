#ifndef LIQUIDOS_TYPES_H
#define LIQUIDOS_TYPES_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

typedef __SIZE_TYPE__ size_t;
typedef __UINTPTR_TYPE__ uintptr_t;

typedef enum bool {
    false = 0,
    true = 1
} bool;

#define NULL ((void *)0)

#endif
