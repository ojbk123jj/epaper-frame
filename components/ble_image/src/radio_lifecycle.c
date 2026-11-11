#include "radio_lifecycle.h"
int image_radio_pause(image_radio_t *radio,const image_radio_ops_t *ops)
{
    if(radio->state==IMAGE_RADIO_OFF)return 0;
    if(radio->state!=IMAGE_RADIO_RUNNING)return -1;
    /* Host stop can report success on a link-termination timeout. Controller
     * deinitialization (and its successful hardware disable) is the barrier. */
    if(ops->stop_host()!=0||ops->deinit_controller()!=0){radio->state=IMAGE_RADIO_BROKEN;return -1;}
    radio->state=IMAGE_RADIO_OFF;return 0;
}
int image_radio_resume(image_radio_t *radio,const image_radio_ops_t *ops)
{
    if(radio->state==IMAGE_RADIO_RUNNING)return 0;
    if(radio->state!=IMAGE_RADIO_OFF)return -1;
    if(ops->start()!=0){radio->state=IMAGE_RADIO_BROKEN;return -1;}
    radio->state=IMAGE_RADIO_RUNNING;return 0;
}
