#pragma once

#include "../kernel/types.hpp"

namespace drivers {

bool e1000_init();
bool e1000_send_packet(const void* data, uint16_t length);
uint16_t e1000_recv_packet(void* dest, uint16_t max_len);
void e1000_get_mac(uint8_t* mac);

} // namespace drivers
