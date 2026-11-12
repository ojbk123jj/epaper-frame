#pragma once
#include "frame_transfer.h"
uint16_t ble_image_write_limit(uint16_t mtu);
/* Maximum payload after the 4-byte offset in a preview read value. */
uint16_t ble_image_read_limit(uint16_t mtu);
frame_result_t ble_image_session_error(frame_result_t previous,bool owned,frame_result_t result,bool began);
