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
    if (!p || length!=15 || p[0]!=1 || p[1]!=1 || !u32(p+2) || p[14]>1) return FRAME_PACKET;
    if (rx->active) return FRAME_BUSY;
    if (u32(p+6)!=FRAME_BYTES || !rx->buffer || rx->capacity<FRAME_BYTES) return FRAME_SIZE;
    rx->job_id=u32(p+2); rx->checksum=u32(p+10); rx->mode=p[14];
    rx->received=0; rx->touched_ms=now; rx->active=true;
    return FRAME_OK;
}
frame_result_t frame_rx_write(frame_rx_t *rx, const uint8_t *p, size_t length, uint64_t now)
{
    if (!p || length<=4) return FRAME_PACKET;
    if (!rx->active) return FRAME_STATE;
    if (u32(p)!=rx->received) return FRAME_OFFSET;
    if (length-4 > FRAME_BYTES-rx->received) return FRAME_SIZE;
    memcpy(rx->buffer+rx->received,p+4,length-4);
    rx->received+=length-4; rx->touched_ms=now;
    return FRAME_OK;
}
frame_result_t frame_rx_commit(frame_rx_t *rx, const uint8_t *p, size_t length)
{
    if (!p || length!=6 || p[0]!=2 || p[1]!=1) return FRAME_PACKET;
    if (!rx->active || u32(p+2)!=rx->job_id) return FRAME_STATE;
    if (rx->received!=FRAME_BYTES) return FRAME_SIZE;
    if (frame_crc32(rx->buffer,rx->received)!=rx->checksum) return FRAME_CHECKSUM;
    rx->active=false;
    return FRAME_OK;
}
void frame_rx_abort(frame_rx_t *rx) { rx->active=false; rx->received=0; }
bool frame_rx_expire(frame_rx_t *rx, uint64_t now)
{
    if (!rx->active || now<rx->touched_ms || now-rx->touched_ms<FRAME_RX_TIMEOUT_MS) return false;
    frame_rx_abort(rx); return true;
}
