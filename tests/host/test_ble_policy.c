#include <assert.h>
#include <stdio.h>
#include "radio_lifecycle.h"
#include "ble_policy.h"
static unsigned order,stop_order,deinit_order,start_order;
static int stop_error,deinit_error,start_error;
static int stop(void){stop_order=++order;return stop_error;}
static int deinit(void){deinit_order=++order;return deinit_error;}
static int start(void){start_order=++order;return start_error;}
int main(void)
{
    image_radio_t radio={.state=IMAGE_RADIO_RUNNING};
    const image_radio_ops_t ops={stop,deinit,start};
    assert(image_radio_pause(&radio,&ops)==0);
    assert(stop_order==1&&deinit_order==2&&radio.state==IMAGE_RADIO_OFF);
    assert(image_radio_resume(&radio,&ops)==0&&start_order==3);
    stop_error=-1;
    assert(image_radio_pause(&radio,&ops)!=0);
    assert(radio.state==IMAGE_RADIO_BROKEN&&image_radio_resume(&radio,&ops)!=0);
    stop_error=0;deinit_error=-1;radio.state=IMAGE_RADIO_RUNNING;
    assert(image_radio_pause(&radio,&ops)!=0&&radio.state==IMAGE_RADIO_BROKEN);
    deinit_error=0;start_error=-1;radio.state=IMAGE_RADIO_OFF;
    assert(image_radio_resume(&radio,&ops)!=0&&radio.state==IMAGE_RADIO_BROKEN);
    assert(ble_image_write_limit(23)==20&&ble_image_write_limit(247)==244&&ble_image_write_limit(517)==244);
    assert(ble_image_read_limit(23)==18&&ble_image_read_limit(247)==240&&ble_image_read_limit(517)==240);
    assert(ble_image_read_limit(5)==0&&ble_image_read_limit(6)==1);
    assert(ble_image_session_error(FRAME_OK,false,FRAME_STATE,false)==FRAME_OK);
    assert(ble_image_session_error(FRAME_TIMEOUT,false,FRAME_PACKET,false)==FRAME_TIMEOUT);
    assert(ble_image_session_error(FRAME_OK,true,FRAME_OFFSET,false)==FRAME_OFFSET);
    assert(ble_image_session_error(FRAME_OFFSET,true,FRAME_OK,false)==FRAME_OK);
    assert(ble_image_session_error(FRAME_TIMEOUT,false,FRAME_OK,true)==FRAME_OK);
    puts("BLE controller-off lifecycle and session policy tests passed");
}
