#include "kernel/types.hpp"

extern "C" {

void* memcpy(void* dest, const void* src, size_t n) {
    char* d = (char*)dest;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; ++i) {
        d[i] = s[i];
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    char* p = (char*)s;
    for (size_t i = 0; i < n; ++i) {
        p[i] = (char)c;
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
