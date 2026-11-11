#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "frame_service.h"
#include "mock.h"
#include "epaper_display.h"
#include "image_convert.h"

static unsigned calls, pauses, resumes;
static int display_result, pause_result;
static uint8_t shown[30000], queued[2048];
static size_t queue_size;
static bool has_job;
static jmp_buf worker_end;
static void (*worker_fn)(void *);
void *xSemaphoreCreateMutex(void){return malloc(1);}
int xSemaphoreTake(void *p,unsigned t){(void)p;(void)t;return 1;}
int xSemaphoreGive(void *p){(void)p;return 1;}
void vSemaphoreDelete(void *p){free(p);}
void *xQueueCreate(unsigned n,size_t s){(void)n;queue_size=s;assert(s<=sizeof(queued));return malloc(1);}
int xQueueSend(void *q,const void *p,unsigned t){(void)q;(void)t;memcpy(queued,p,queue_size);has_job=true;return 1;}
int xQueueReceive(void *q,void *p,unsigned t){(void)q;(void)t;if(!has_job)longjmp(worker_end,1);memcpy(p,queued,queue_size);has_job=false;return 1;}
void vQueueDelete(void *p){free(p);}
int xTaskCreate(void (*f)(void *),const char *n,unsigned s,void *a,unsigned p,void *h)
{worker_fn=f;(void)n;(void)s;(void)a;(void)p;(void)h;return 1;}
void vTaskDelay(unsigned t){(void)t;}
esp_err_t network_service_prepare(void){return ESP_OK;}
esp_err_t network_service_suspend(void){return ESP_OK;}
esp_err_t modelscope_generate_image(const char *p,char **u){(void)p;(void)u;return ESP_FAIL;}
esp_err_t modelscope_download_image(const char *u,uint8_t **d,size_t *n){(void)u;(void)d;(void)n;return ESP_FAIL;}
esp_err_t image_decode_rgb(const uint8_t *d,size_t n,uint8_t **r,int *w,int *h)
{(void)d;(void)n;(void)r;(void)w;(void)h;return ESP_FAIL;}
esp_err_t image_rgb_to_frame(const uint8_t *r,size_t n,int w,int h,bool d,uint8_t *f,size_t l)
{(void)r;(void)n;(void)w;(void)h;(void)d;(void)f;(void)l;return ESP_FAIL;}
esp_err_t epaper_display_show(const uint8_t *f,size_t n,epd_mode_t m)
{(void)m;assert(n==30000);memcpy(shown,f,n);calls++;return display_result;}
static esp_err_t guard(bool quiet){if(quiet){pauses++;return pause_result;}resumes++;return ESP_OK;}

static void run_queued(void){if(!setjmp(worker_end))worker_fn(NULL);}
int main(void)
{
    assert(frame_service_start()==ESP_OK);
    frame_service_set_radio_guard(guard);
    frame_status_t status;
    frame_service_status(&status);
    bool configured=status.configured;
    if(!configured)assert(frame_service_submit_ai("hello")==ESP_ERR_INVALID_STATE);
    assert(frame_service_reserve(7)==ESP_OK);
    assert(frame_service_reserve(8)==ESP_ERR_INVALID_STATE);
    assert(frame_service_submit_ai("hello")==ESP_ERR_INVALID_STATE);
    assert(frame_service_cancel(8,ESP_FAIL)==ESP_ERR_INVALID_STATE);
    static uint8_t frame[30000];memset(frame,0x1b,sizeof(frame));
    assert(frame_service_commit(8,frame,30000,EPD_MODE_FULL)==ESP_ERR_INVALID_STATE);
    assert(frame_service_commit(7,frame,29999,EPD_MODE_FULL)==ESP_ERR_INVALID_ARG);
    assert(frame_service_commit(7,frame,30000,EPD_MODE_FULL)==ESP_OK);
    frame[0]=0;
    assert(frame_service_cancel(7,ESP_FAIL)==ESP_ERR_INVALID_STATE);
    run_queued();
    assert(calls==1&&shown[0]==0x1b&&pauses==1&&resumes==1);
    frame_service_status(&status);
    assert(!status.busy&&status.stage==FRAME_SUCCEEDED&&status.job_id==7&&status.displayed_count==1);
    assert(frame_service_reserve(9)==ESP_OK);
    assert(frame_service_cancel(9,ESP_ERR_TIMEOUT)==ESP_OK);
    assert(frame_service_reserve(10)==ESP_OK);
    assert(frame_service_commit(10,frame,30000,EPD_MODE_FULL)==ESP_OK);
    pause_result=ESP_FAIL;run_queued();assert(calls==1);
    frame_service_status(&status);assert(status.stage==FRAME_FAILED&&!status.busy);
    pause_result=ESP_OK;
    if(configured) {
        assert(frame_service_submit_ai("hello")==ESP_OK);
        assert(frame_service_reserve(11)==ESP_ERR_INVALID_STATE);
        run_queued();frame_service_status(&status);assert(!status.busy&&status.stage==FRAME_FAILED);
    }
    puts("Shared AI/BLE frame ownership tests passed");
}
