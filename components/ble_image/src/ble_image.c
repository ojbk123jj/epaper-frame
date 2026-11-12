#include "ble_image.h"
#include <string.h>
#include "app_config.h"
#include "frame_transfer.h"
#include "frame_service.h"
#include "radio_lifecycle.h"
#include "ble_policy.h"
#include "esp_bt.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* UUID bytes are little endian. See docs/ble-protocol.md for canonical strings. */
#define FRAME_UUID(n) BLE_UUID128_INIT(0x01,0x40,0xb7,0xe5,0xd3,0x61,0xc8,0xa6,0x2b,0x4f,0x7d,0x9c,n,0x00,0xe1,0xf0)
static const ble_uuid128_t s_service_uuid=FRAME_UUID(0),s_info_uuid=FRAME_UUID(1),
    s_control_uuid=FRAME_UUID(2),s_data_uuid=FRAME_UUID(3),s_status_uuid=FRAME_UUID(4),
    s_ai_uuid=FRAME_UUID(5),s_preview_uuid=FRAME_UUID(6);
static const char *TAG="ble_image";
static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
#define DISCONNECTED BIT0
#define HOST_EXIT BIT1
#define STACK_READY BIT2
static frame_rx_t s_rx;
static uint16_t s_conn=BLE_HS_CONN_HANDLE_NONE,s_status_handle;
static uint8_t s_addr_type;
static bool s_synced,s_paused;
static frame_result_t s_error;
static esp_timer_handle_t s_timer;
static image_radio_t s_radio={.state=IMAGE_RADIO_RUNNING};
static uint8_t *s_preview;
static size_t s_preview_offset;
static bool s_preview_ready;
static int start_stack(void);

_Static_assert(FRAME_PROMPT_MAX==APP_AI_MAX_PROMPT,"AI prompt limit mismatch");

static int gap_event(struct ble_gap_event *event,void *arg);
static void put16(uint8_t *p,uint16_t v){p[0]=v;p[1]=v>>8;}
static void put32(uint8_t *p,uint32_t v){for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}

/* All adapter state is protected by s_lock. The shared service never calls the
 * radio guard while holding its own mutex, maintaining BLE -> service order. */
static int advertise_locked(void)
{
    if(!s_synced||s_paused||s_conn!=BLE_HS_CONN_HANDLE_NONE||ble_gap_adv_active())return 0;
    struct ble_hs_adv_fields fields={0};
    fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128=(ble_uuid128_t *)&s_service_uuid;fields.num_uuids128=1;fields.uuids128_is_complete=1;
    int rc=ble_gap_adv_set_fields(&fields);if(rc)return rc;
    struct ble_hs_adv_fields scan={0};
    const char *name=ble_svc_gap_device_name();
    scan.name=(uint8_t *)name;scan.name_len=strlen(name);scan.name_is_complete=1;
    rc=ble_gap_adv_rsp_set_fields(&scan);if(rc)return rc;
    struct ble_gap_adv_params params={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN};
    return ble_gap_adv_start(s_addr_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
}
static void cancel_locked(esp_err_t reason)
{
    /* A prompt upload never reserves the shared slot; only an image receive does. */
    if(s_rx.active&&s_rx.kind==FRAME_KIND_IMAGE)frame_service_cancel(s_rx.job_id,reason);
    frame_rx_abort(&s_rx);
}
static void ai_status_locked(uint8_t p[16])
{
    frame_status_t st;frame_service_status(&st);
    memset(p,0,16);p[0]=1;
    uint8_t state=0;
    if(st.source==FRAME_SOURCE_AI) {
        switch(st.stage) {
        case FRAME_QUEUED:state=1;break;
        case FRAME_CONNECTING:state=2;break;
        case FRAME_GENERATING:state=3;break;
        case FRAME_DOWNLOADING:state=4;break;
        case FRAME_CONVERTING:state=5;break;
        case FRAME_DISPLAYING:state=6;break;
        case FRAME_SUCCEEDED:state=7;break;
        case FRAME_FAILED:state=8;break;
        default:break;
        }
    }
    p[1]=state;
    put16(p+2,(uint16_t)st.last_error);
    put32(p+4,st.source==FRAME_SOURCE_AI?st.job_id:0);
    uint32_t elapsed=st.elapsed_ms/1000;if(elapsed>0xffff)elapsed=0xffff;
    put16(p+8,(uint16_t)elapsed);
    p[10]=(uint8_t)st.failure_stage;
    p[11]=st.has_ai_preview?1:0;
    put32(p+12,st.last_ai_job_id);
}
static void status_locked(uint8_t p[20])
{
    frame_status_t st;frame_service_status(&st);
    memset(p,0,20);p[0]=1;
    uint8_t state=0;
    if(st.source==FRAME_SOURCE_AI)state=st.busy?6:0;
    else if(st.source==FRAME_SOURCE_BLE) {
        switch(st.stage) {
        case FRAME_RECEIVING:state=1;break;
        case FRAME_QUEUED:state=2;break;
        case FRAME_DISPLAYING:state=3;break;
        case FRAME_SUCCEEDED:state=4;break;
        case FRAME_FAILED:state=5;break;
        default:break;
        }
    }
    p[1]=state;
    put16(p+2,(state==1||state==5)&&s_error?s_error:(state==5&&st.last_error!=ESP_OK?FRAME_STATE:FRAME_OK));
    put32(p+4,st.source==FRAME_SOURCE_BLE?st.job_id:0);
    put32(p+8,s_rx.active?(uint32_t)s_rx.received:(state>=2&&state<=4?FRAME_BYTES:0));
    put32(p+12,FRAME_BYTES);put32(p+16,st.displayed_count);
}
static int access_gatt(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctxt,void *arg)
{
    (void)attr;int kind=(int)(intptr_t)arg,rc=0;uint8_t p[244];
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR&&kind==1) {
        frame_status_t st;frame_service_status(&st);
        memset(p,0,16);p[0]=1;p[1]=4;put16(p+2,400);put16(p+4,300);put32(p+6,FRAME_BYTES);
        put16(p+10,ble_image_write_limit(ble_att_mtu(conn)));p[12]=3;
        p[13]=(uint8_t)(1|(st.configured?2:0)|4);
        put16(p+14,(uint16_t)APP_AI_MAX_PROMPT);
        rc=os_mbuf_append(ctxt->om,p,16)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    } else if(ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR&&kind==4) {
        status_locked(p);rc=os_mbuf_append(ctxt->om,p,20)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    } else if(ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR&&kind==5) {
        ai_status_locked(p);rc=os_mbuf_append(ctxt->om,p,16)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    } else if(ctxt->op==BLE_GATT_ACCESS_OP_READ_CHR&&kind==6) {
        if(!s_preview_ready) {
            rc=BLE_ATT_ERR_UNLIKELY;
        } else {
            size_t limit=ble_image_read_limit(ble_att_mtu(conn));
            size_t remaining=FRAME_BYTES-s_preview_offset;
            size_t n=remaining<limit?remaining:limit;
            put32(p,(uint32_t)s_preview_offset);
            if(n)memcpy(p+4,s_preview+s_preview_offset,n);
            rc=os_mbuf_append(ctxt->om,p,4+n)?BLE_ATT_ERR_INSUFFICIENT_RES:0;
            s_preview_offset+=n;
            if(s_preview_offset>=FRAME_BYTES)s_preview_ready=false;
        }
    } else if(ctxt->op==BLE_GATT_ACCESS_OP_WRITE_CHR&&(kind==2||kind==3)) {
        size_t len=OS_MBUF_PKTLEN(ctxt->om);
        if(s_paused||len>ble_image_write_limit(ble_att_mtu(conn))||os_mbuf_copydata(ctxt->om,0,len,p)) {
            rc=BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        } else {
            frame_result_t result=FRAME_PACKET;
            bool owned=s_rx.active,began=false;
            if(kind==3)result=frame_rx_write(&s_rx,p,len,esp_timer_get_time()/1000);
            else if(len==15&&p[0]==1) {
                result=frame_rx_begin(&s_rx,p,len,esp_timer_get_time()/1000);
                if(result==FRAME_OK&&frame_service_reserve(s_rx.job_id)!=ESP_OK) {
                    frame_rx_abort(&s_rx);result=FRAME_BUSY;
                }
                began=result==FRAME_OK;
            } else if(len==15&&p[0]==4) {
                frame_status_t st;frame_service_status(&st);
                if(!st.configured||st.busy) {
                    result=FRAME_BUSY;
                } else {
                    result=frame_rx_begin(&s_rx,p,len,esp_timer_get_time()/1000);
                    began=result==FRAME_OK;
                }
            } else if(len==6&&p[0]==2) {
                result=frame_rx_commit(&s_rx,p,len);
                if(result==FRAME_OK) {
                    esp_err_t err=frame_service_commit(s_rx.job_id,s_rx.buffer,FRAME_BYTES,
                        s_rx.mode?EPD_MODE_FAST:EPD_MODE_FULL);
                    if(err!=ESP_OK){frame_service_cancel(s_rx.job_id,err);result=FRAME_STATE;}
                }
            } else if(len==6&&p[0]==5) {
                result=frame_rx_commit(&s_rx,p,len);
                if(result==FRAME_OK&&s_rx.kind==FRAME_KIND_AI_PROMPT) {
                    if(memchr(s_rx.buffer,0,s_rx.received)) {
                        result=FRAME_PACKET;
                    } else {
                        s_rx.buffer[s_rx.received]='\0';
                        esp_err_t err=frame_service_submit_ai_id(s_rx.job_id,(const char *)s_rx.buffer);
                        if(err!=ESP_OK)result=FRAME_STATE;
                    }
                } else if(result==FRAME_OK) {
                    result=FRAME_STATE;
                }
            } else if(len==6&&p[0]==6&&p[1]==1) {
                s_preview_ready=false;
                if(!s_preview||frame_service_copy_last_ai_frame(get32(p+2),s_preview)!=ESP_OK) {
                    result=FRAME_STATE;
                } else {
                    s_preview_offset=0;s_preview_ready=true;result=FRAME_OK;
                }
            } else if(len==6&&p[0]==3&&p[1]==1&&s_rx.active&&get32(p+2)==s_rx.job_id) {
                cancel_locked(ESP_FAIL);result=FRAME_OK;
            }
            s_error=ble_image_session_error(s_error,owned,result,began);
            if(result!=FRAME_OK)rc=BLE_ATT_ERR_UNLIKELY;
        }
    } else rc=BLE_ATT_ERR_UNLIKELY;
    xSemaphoreGive(s_lock);
    if(kind==2&&ctxt->op==BLE_GATT_ACCESS_OP_WRITE_CHR)ble_gatts_chr_updated(s_status_handle);
    return rc;
}
static const struct ble_gatt_svc_def s_services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=&s_service_uuid.u,.characteristics=(struct ble_gatt_chr_def[]){
        {.uuid=&s_info_uuid.u,.access_cb=access_gatt,.arg=(void *)1,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=&s_control_uuid.u,.access_cb=access_gatt,.arg=(void *)2,.flags=BLE_GATT_CHR_F_WRITE},
        {.uuid=&s_data_uuid.u,.access_cb=access_gatt,.arg=(void *)3,.flags=BLE_GATT_CHR_F_WRITE},
        {.uuid=&s_status_uuid.u,.access_cb=access_gatt,.arg=(void *)4,.flags=BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY,.val_handle=&s_status_handle},
        {.uuid=&s_ai_uuid.u,.access_cb=access_gatt,.arg=(void *)5,.flags=BLE_GATT_CHR_F_READ},
        {.uuid=&s_preview_uuid.u,.access_cb=access_gatt,.arg=(void *)6,.flags=BLE_GATT_CHR_F_READ},
        {0}}}, {0}
};
static int gap_event(struct ble_gap_event *event,void *arg)
{
    (void)arg;xSemaphoreTake(s_lock,portMAX_DELAY);
    switch(event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if(event->connect.status==0) {
            s_conn=event->connect.conn_handle;xEventGroupClearBits(s_events,DISCONNECTED);
            if(s_paused)ble_gap_terminate(s_conn,BLE_ERR_REM_USER_CONN_TERM);
        } else advertise_locked();
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        cancel_locked(ESP_FAIL);s_conn=BLE_HS_CONN_HANDLE_NONE;
        xEventGroupSetBits(s_events,DISCONNECTED);advertise_locked();break;
    case BLE_GAP_EVENT_ADV_COMPLETE:advertise_locked();break;
    default:break;
    }
    xSemaphoreGive(s_lock);return 0;
}
static int stop_host(void)
{
    xEventGroupClearBits(s_events,HOST_EXIT|STACK_READY);
    int rc=nimble_port_stop();
    if(rc)return rc;
    if(!(xEventGroupWaitBits(s_events,HOST_EXIT,pdFALSE,pdFALSE,pdMS_TO_TICKS(3000))&HOST_EXIT))return ESP_ERR_TIMEOUT;
    /* The host handed off after leaving nimble_port_run and uses no more stack
     * state. Delete it from this owning task before freeing NimBLE resources. */
    nimble_port_freertos_deinit();
    return 0;
}
static int deinit_controller(void)
{
    esp_err_t err=nimble_port_deinit();
    if(err!=ESP_OK||esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE)return ESP_FAIL;
    xSemaphoreTake(s_lock,portMAX_DELAY);s_synced=false;s_conn=BLE_HS_CONN_HANDLE_NONE;xSemaphoreGive(s_lock);
    return 0;
}
static esp_err_t radio_guard(bool quiet)
{
    const image_radio_ops_t ops={stop_host,deinit_controller,start_stack};
    xSemaphoreTake(s_lock,portMAX_DELAY);s_paused=true;xSemaphoreGive(s_lock);
    int rc=quiet?image_radio_pause(&s_radio,&ops):image_radio_resume(&s_radio,&ops);
    if(!quiet&&rc==0) {
        xSemaphoreTake(s_lock,portMAX_DELAY);s_paused=false;
        rc=advertise_locked();xSemaphoreGive(s_lock);
    }
    if(rc){ESP_LOGE(TAG,"radio lifecycle failed; restart required: %d",rc);return ESP_FAIL;}
    return ESP_OK;
}
static void expire_receive(void *arg)
{
    (void)arg;bool expired=false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(s_paused){xSemaphoreGive(s_lock);return;}
    if(s_rx.active&&(uint64_t)(esp_timer_get_time()/1000)-s_rx.touched_ms>=FRAME_RX_TIMEOUT_MS) {
        cancel_locked(ESP_ERR_TIMEOUT);s_error=FRAME_TIMEOUT;expired=true;
    }
    /* Retry advertising after transient host/controller failures. */
    if(!s_paused)advertise_locked();
    if(expired)ble_gatts_chr_updated(s_status_handle);
    xSemaphoreGive(s_lock);
}
static void on_sync(void)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    int rc=ble_hs_util_ensure_addr(0);
    if(!rc)rc=ble_hs_id_infer_auto(0,&s_addr_type);
    s_synced=rc==0;
    if(!rc)rc=advertise_locked();
    if(!rc)xEventGroupSetBits(s_events,STACK_READY);
    xSemaphoreGive(s_lock);
    if(rc)ESP_LOGE(TAG,"BLE sync/advertising failed: %d",rc);
}
static void on_reset(int reason)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);s_synced=false;s_conn=BLE_HS_CONN_HANDLE_NONE;
    cancel_locked(ESP_FAIL);xEventGroupSetBits(s_events,DISCONNECTED);xSemaphoreGive(s_lock);
    ESP_LOGE(TAG,"host reset: %d",reason);
}
static void host_task(void *arg)
{
    (void)arg;nimble_port_run();
    xEventGroupSetBits(s_events,HOST_EXIT);
    vTaskSuspend(NULL); /* Owner deletes this task before port deinitialization. */
}
static int start_stack(void)
{
    xEventGroupClearBits(s_events,STACK_READY|HOST_EXIT);
    esp_err_t err=nimble_port_init();if(err!=ESP_OK)return err;
    ble_hs_cfg.sync_cb=on_sync;ble_hs_cfg.reset_cb=on_reset;
    ble_svc_gap_init();ble_svc_gatt_init();
    int rc=ble_svc_gap_device_name_set("EPaperFrame");
    if(!rc)rc=ble_att_set_preferred_mtu(247);
    if(!rc)rc=ble_gatts_count_cfg(s_services);
    if(!rc)rc=ble_gatts_add_svcs(s_services);
    if(rc)return rc;
    nimble_port_freertos_init(host_task);
    return (xEventGroupWaitBits(s_events,STACK_READY,pdFALSE,pdFALSE,pdMS_TO_TICKS(5000))&STACK_READY)?0:ESP_ERR_TIMEOUT;
}
esp_err_t ble_image_start(void)
{
    if(s_lock)return ESP_ERR_INVALID_STATE;
    s_lock=xSemaphoreCreateMutex();s_events=xEventGroupCreate();
    uint8_t *buffer=heap_caps_malloc(FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    s_preview=heap_caps_malloc(FRAME_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_lock||!s_events||!buffer||!s_preview){free(buffer);free(s_preview);s_preview=NULL;return ESP_ERR_NO_MEM;}
    s_preview_offset=0;s_preview_ready=false;
    frame_rx_init(&s_rx,buffer,FRAME_BYTES);xEventGroupSetBits(s_events,DISCONNECTED);
    esp_err_t err=nvs_flash_init();if(err!=ESP_OK)return err;
    int rc=start_stack();if(rc)return ESP_FAIL;
    const esp_timer_create_args_t timer={.callback=expire_receive,.name="frame_rx"};
    err=esp_timer_create(&timer,&s_timer);if(err!=ESP_OK)return err;
    err=esp_timer_start_periodic(s_timer,1000000);if(err!=ESP_OK)return err;
    frame_service_set_radio_guard(radio_guard);
    ESP_LOGI(TAG,"BLE image receiver ready: EPaperFrame");return ESP_OK;
}
