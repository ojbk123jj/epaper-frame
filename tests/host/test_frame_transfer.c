#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "frame_transfer.h"

static void put32(uint8_t *p, uint32_t v) { for (int i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
int main(void)
{
    static uint8_t buffer[30000], input[30000];
    memset(input, 0x1b, sizeof(input));
    assert(frame_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926U);
    frame_rx_t rx;
    frame_rx_init(&rx, buffer, sizeof(buffer));
    uint8_t begin[15] = {1,1};
    put32(begin+2, 42); put32(begin+6, sizeof(input));
    put32(begin+10, frame_crc32(input, sizeof(input)));
    assert(frame_rx_begin(&rx, begin, 14, 0) == FRAME_PACKET);
    begin[14]=2;
    assert(frame_rx_begin(&rx, begin, 15, 0) == FRAME_PACKET);
    begin[14]=0;
    put32(begin+6, 30001);
    assert(frame_rx_begin(&rx, begin, 15, 0) == FRAME_SIZE);
    put32(begin+6, 30000);
    assert(frame_rx_begin(&rx, begin, 15, 100) == FRAME_OK);
    assert(frame_rx_begin(&rx, begin, 15, 100) == FRAME_BUSY);
    uint8_t data[104] = {0}; memcpy(data+4,input,100);
    put32(data,1);
    assert(frame_rx_write(&rx,data,sizeof(data),200)==FRAME_OFFSET);
    put32(data,0);
    assert(frame_rx_write(&rx,data,4,200)==FRAME_PACKET);
    assert(frame_rx_write(&rx,data,sizeof(data),200)==FRAME_OK);
    uint8_t commit[6]={2,1}; put32(commit+2,42);
    assert(frame_rx_commit(&rx,commit,6)==FRAME_SIZE);
    for (uint32_t offset=100;offset<30000;offset+=100) {
        put32(data,offset);
        assert(frame_rx_write(&rx,data,sizeof(data),200)==FRAME_OK);
    }
    assert(frame_rx_write(&rx,data,sizeof(data),200)==FRAME_OFFSET);
    put32(commit+2,43);
    assert(frame_rx_commit(&rx,commit,6)==FRAME_STATE);
    put32(commit+2,42); buffer[0]^=1;
    assert(frame_rx_commit(&rx,commit,6)==FRAME_CHECKSUM);
    buffer[0]^=1;
    assert(frame_rx_commit(&rx,commit,6)==FRAME_OK);
    assert(!rx.active && !memcmp(buffer,input,30000));
    assert(frame_rx_commit(&rx,commit,6)==FRAME_STATE);
    assert(frame_rx_begin(&rx,begin,15,1000)==FRAME_OK);
    assert(!frame_rx_expire(&rx,30999));
    assert(frame_rx_expire(&rx,31000));
    assert(!rx.active);
    put32(begin+2,0);
    assert(frame_rx_begin(&rx,begin,15,0)==FRAME_PACKET);

    /* AI prompt sessions share the data characteristic but have their own kind. */
    static uint8_t prompt[5]={'h','e','l','l','o'};
    uint8_t ai_begin[15]={4,1}; put32(ai_begin+2,77); put32(ai_begin+6,5);
    put32(ai_begin+10,frame_crc32(prompt,5));
    uint8_t ai_commit[6]={5,1}; put32(ai_commit+2,77);
    frame_rx_init(&rx,buffer,sizeof(buffer));
    ai_begin[14]=1;
    assert(frame_rx_begin(&rx,ai_begin,15,0)==FRAME_PACKET);
    ai_begin[14]=0;
    put32(ai_begin+6,0);
    assert(frame_rx_begin(&rx,ai_begin,15,0)==FRAME_SIZE);
    put32(ai_begin+6,1025);
    assert(frame_rx_begin(&rx,ai_begin,15,0)==FRAME_SIZE);
    put32(ai_begin+6,5);
    assert(frame_rx_begin(&rx,ai_begin,15,0)==FRAME_OK);
    assert(frame_rx_begin(&rx,ai_begin,15,0)==FRAME_BUSY);
    uint8_t prompt_data[9]={0}; memcpy(prompt_data+4,prompt,5);
    assert(frame_rx_write(&rx,prompt_data,9,1)==FRAME_OK);
    assert(frame_rx_write(&rx,prompt_data,9,1)==FRAME_OFFSET);
    uint8_t wrong_commit[6]={2,1}; put32(wrong_commit+2,77);
    assert(frame_rx_commit(&rx,wrong_commit,6)==FRAME_PACKET);
    assert(rx.active);
    assert(frame_rx_commit(&rx,ai_commit,6)==FRAME_OK);
    assert(!rx.active && rx.kind==FRAME_KIND_AI_PROMPT && !memcmp(buffer,prompt,5));
    put32(ai_begin+10,frame_crc32(prompt,5));
    assert(frame_rx_begin(&rx,ai_begin,15,2)==FRAME_OK);
    prompt_data[5]='a';
    put32(prompt_data,0);
    assert(frame_rx_write(&rx,prompt_data,9,3)==FRAME_OK);
    assert(frame_rx_commit(&rx,ai_commit,6)==FRAME_CHECKSUM);
    assert(rx.active);
    frame_rx_abort(&rx);
    assert(rx.kind==FRAME_KIND_NONE && rx.expected==0);
    puts("Frame transfer validation tests passed");
}
