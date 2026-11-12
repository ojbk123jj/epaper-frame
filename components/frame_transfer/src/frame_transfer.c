#include "frame_transfer.h"
#include <string.h>

static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
uint32_t frame_crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i=0;i<length;i++) {
        crc ^= data[i];
        for (int bit=0;bit<8;bit++) crc = (crc>>1) ^ (0xedb88320U & (0U-(crc&1)));
    }
    return ~crc;
}
void frame_rx_init(frame_rx_t *rx, uint8_t *buffer, size_t capacity)
{ *rx = (frame_rx_t){.buffer=buffer,.capacity=capacity}; }
frame_result_t frame_rx_begin(frame_rx_t *rx, const uint8_t *p, size_t length, uint64_t now)
{
    if (!p || length!=15 || p[1]!=1 || !u32(p+2)) return FRAME_PACKET;
    frame_kind_t kind;
    size_t expected=u32(p+6);
    if (p[0]==1) {
        if (p[14]>1) return FRAME_PACKET;
        if (expected!=FRAME_BYTES || !rx->buffer || rx->capacity<FRAME_BYTES) return FRAME_SIZE;
        kind=FRAME_KIND_IMAGE;
    } else if (p[0]==4) {
        /* AI prompt sessions are short UTF-8 payloads; mode is reserved zero. */
        if (p[14]!=0) return FRAME_PACKET;
        if (expected==0 || expected>FRAME_PROMPT_MAX || !rx->buffer || rx->capacity<expected) return FRAME_SIZE;
        kind=FRAME_KIND_AI_PROMPT;
    } else {
        return FRAME_PACKET;
    }
    if (rx->active) return FRAME_BUSY;
    rx->job_id=u32(p+2); rx->checksum=u32(p+10); rx->mode=p[14];
    rx->kind=kind; rx->expected=expected;
    rx->received=0; rx->touched_ms=now; rx->active=true;
    return FRAME_OK;
}
frame_result_t frame_rx_write(frame_rx_t *rx, const uint8_t *p, size_t length, uint64_t now)
{
    if (!p || length<=4) return FRAME_PACKET;
    if (!rx->active) return FRAME_STATE;
    if (u32(p)!=rx->received) return FRAME_OFFSET;
    if (length-4 > rx->expected-rx->received) return FRAME_SIZE;
    memcpy(rx->buffer+rx->received,p+4,length-4);
    rx->received+=length-4; rx->touched_ms=now;
    return FRAME_OK;
}
frame_result_t frame_rx_commit(frame_rx_t *rx, const uint8_t *p, size_t length)
{
    if (!p || length!=6 || p[1]!=1) return FRAME_PACKET;
    if (!rx->active || u32(p+2)!=rx->job_id) return FRAME_STATE;
    uint8_t expected_op=rx->kind==FRAME_KIND_IMAGE?2:rx->kind==FRAME_KIND_AI_PROMPT?5:0;
    if (!expected_op || p[0]!=expected_op) return FRAME_PACKET;
    if (rx->received!=rx->expected) return FRAME_SIZE;
    if (frame_crc32(rx->buffer,rx->received)!=rx->checksum) return FRAME_CHECKSUM;
    rx->active=false;
    return FRAME_OK;
}
void frame_rx_abort(frame_rx_t *rx) { rx->active=false; rx->received=0; rx->expected=0; rx->kind=FRAME_KIND_NONE; }
bool frame_rx_expire(frame_rx_t *rx, uint64_t now)
{
    if (!rx->active || now<rx->touched_ms || now-rx->touched_ms<FRAME_RX_TIMEOUT_MS) return false;
    frame_rx_abort(rx); return true;
}
