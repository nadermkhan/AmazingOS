#include "kernel/types.hpp"

extern "C" {

void* memcpy(void* dest, const void* src, size_t n) {
    if (((uintptr_t)dest % 8 == 0) && ((uintptr_t)src % 8 == 0)) {
        uint64_t* d64 = (uint64_t*)dest;
        const uint64_t* s64 = (const uint64_t*)src;
        size_t n64 = n / 8;
        for (size_t i = 0; i < n64; ++i) {
            d64[i] = s64[i];
        }
        char* d = (char*)(d64 + n64);
        const char* s = (const char*)(s64 + n64);
        size_t rem = n % 8;
        for (size_t i = 0; i < rem; ++i) {
            d[i] = s[i];
        }
    } else if (((uintptr_t)dest % 4 == 0) && ((uintptr_t)src % 4 == 0)) {
        uint32_t* d32 = (uint32_t*)dest;
        const uint32_t* s32 = (const uint32_t*)src;
        size_t n32 = n / 4;
        for (size_t i = 0; i < n32; ++i) {
            d32[i] = s32[i];
        }
        char* d = (char*)(d32 + n32);
        const char* s = (const char*)(s32 + n32);
        size_t rem = n % 4;
        for (size_t i = 0; i < rem; ++i) {
            d[i] = s[i];
        }
    } else {
        char* d = (char*)dest;
        const char* s = (const char*)src;
        for (size_t i = 0; i < n; ++i) {
            d[i] = s[i];
        }
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    char* p = (char*)s;
    uint8_t val = (uint8_t)c;
    uint64_t val64 = ((uint64_t)val << 56) | ((uint64_t)val << 48) | ((uint64_t)val << 40) | ((uint64_t)val << 32) |
                     ((uint64_t)val << 24) | ((uint64_t)val << 16) | ((uint64_t)val << 8) | val;
                     
    if ((uintptr_t)s % 8 == 0) {
        uint64_t* p64 = (uint64_t*)s;
        size_t n64 = n / 8;
        for (size_t i = 0; i < n64; ++i) {
            p64[i] = val64;
        }
        p = (char*)(p64 + n64);
        size_t rem = n % 8;
        for (size_t i = 0; i < rem; ++i) {
            p[i] = val;
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            p[i] = val;
        }
    }
    return s;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) {
        len++;
    }
    return len;
}

double fabs(double x) {
    return x < 0 ? -x : x;
}

float fabsf(float x) {
    return x < 0 ? -x : x;
}

double floor(double x) {
    int i = (int)x;
    return x < i ? i - 1 : i;
}

float floorf(float x) {
    int i = (int)x;
    return x < i ? i - 1 : i;
}

double ceil(double x) {
    int i = (int)x;
    return x > i ? i + 1 : i;
}

float ceilf(float x) {
    int i = (int)x;
    return x > i ? i + 1 : i;
}

double sqrt(double x) {
    if (x <= 0) return 0;
    double val = x;
    for (int i = 0; i < 20; ++i) {
        val = 0.5 * (val + x / val);
    }
    return val;
}

float sqrtf(float x) {
    if (x <= 0) return 0;
    float val = x;
    for (int i = 0; i < 15; ++i) {
        val = 0.5f * (val + x / val);
    }
    return val;
}

double pow(double base, double exp) {
    if (exp == 0) return 1.0;
    if (exp < 0) return 1.0 / pow(base, -exp);
    double res = 1.0;
    int iexp = (int)exp;
    for (int i = 0; i < iexp; ++i) {
        res *= base;
    }
    return res;
}

double fmod(double x, double y) {
    if (y == 0.0) return 0.0;
    double quotient = x / y;
    int iq = (int)quotient;
    return x - iq * y;
}

double cos(double x) {
    double pi = 3.141592653589793;
    while (x > pi) x -= 2.0 * pi;
    while (x < -pi) x += 2.0 * pi;
    double x2 = x * x;
    return 1.0 - x2 / 2.0 + (x2 * x2) / 24.0 - (x2 * x2 * x2) / 720.0;
}

double acos(double x) {
    if (x < -1.0) x = -1.0;
    if (x > 1.0) x = 1.0;
    double pi = 3.141592653589793;
    double x3 = x * x * x;
    double x5 = x3 * x * x;
    return pi / 2.0 - x - x3 / 6.0 - 0.075 * x5;
}

} // extern "C"
