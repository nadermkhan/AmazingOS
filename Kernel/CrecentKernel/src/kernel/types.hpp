#pragma once

// Explicitly sized integer types
typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

typedef signed char        int8_t;
typedef signed short       int16_t;
typedef signed int         int32_t;
typedef signed long long   int64_t;

// Size types
typedef unsigned long long size_t;
typedef signed long long   ssize_t;

// Pointer representation types
typedef unsigned long long uintptr_t;
typedef signed long long   intptr_t;

// Null pointer type
typedef decltype(nullptr)  nullptr_t;

// Window event types
struct Event {
    int type;
    int mx, my;
    char key;
};
