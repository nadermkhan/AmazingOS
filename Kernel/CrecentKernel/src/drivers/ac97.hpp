#pragma once
#include <stdint.h>

namespace drivers {

class AC97 {
public:
    static bool init();
    static void play(uint32_t phys_addr, uint32_t size_bytes);
    static void stop();
    static bool is_active();
    static uint8_t get_civ();
    static void set_lvi(uint8_t lvi);
    static void write_buffer(int buffer_idx, const int16_t* samples, int word_count);
};

} // namespace drivers
