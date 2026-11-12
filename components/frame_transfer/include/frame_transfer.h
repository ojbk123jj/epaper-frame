#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FRAME_BYTES 30000U
#define FRAME_PROMPT_MAX 1024U
#define FRAME_RX_TIMEOUT_MS 30000U
typedef enum {
    FRAME_OK, FRAME_PACKET, FRAME_BUSY, FRAME_SIZE, FRAME_CHECKSUM,
    FRAME_OFFSET, FRAME_TIMEOUT, FRAME_STATE
} frame_result_t;
typedef enum {
    FRAME_KIND_NONE = 0,
    FRAME_KIND_IMAGE,
    FRAME_KIND_AI_PROMPT,
} frame_kind_t;
typedef struct {
    uint8_t *buffer;
    size_t capacity, received, expected;
    uint32_t job_id, checksum;
    uint64_t touched_ms;
    uint8_t mode;
    frame_kind_t kind;
    bool active;
} frame_rx_t;
uint32_t frame_crc32(const uint8_t *data, size_t length);
void frame_rx_init(frame_rx_t *rx, uint8_t *buffer, size_t capacity);
frame_result_t frame_rx_begin(frame_rx_t *rx, const uint8_t *packet, size_t length, uint64_t now_ms);
frame_result_t frame_rx_write(frame_rx_t *rx, const uint8_t *packet, size_t length, uint64_t now_ms);
frame_result_t frame_rx_commit(frame_rx_t *rx, const uint8_t *packet, size_t length);
void frame_rx_abort(frame_rx_t *rx);
bool frame_rx_expire(frame_rx_t *rx, uint64_t now_ms);
