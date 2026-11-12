#include "ble_policy.h"
uint16_t ble_image_write_limit(uint16_t mtu)
{return mtu>247?244:mtu>=23?mtu-3:20;}
uint16_t ble_image_read_limit(uint16_t mtu)
{
    if(mtu<=5)return 0;
    uint16_t limit=mtu-5;
    return limit>240?240:limit;
}
frame_result_t ble_image_session_error(frame_result_t previous,bool owned,frame_result_t result,bool began)
{return began?FRAME_OK:owned?result:previous;}
