#include "frame_service.h"
#include <stdlib.h>
#include <string.h>
#include "app_config.h"
#include "modelscope_client.h"
#include "network_service.h"
#include "image_convert.h"
#include "image_decode.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef struct {
    char prompt[APP_AI_MAX_PROMPT+1];
    uint8_t *frame;
    epd_mode_t mode;
    uint32_t job_id;
} frame_job_t;
static const char *TAG="frame";
static QueueHandle_t s_jobs;
static SemaphoreHandle_t s_lock;
static frame_status_t s_status;
static frame_radio_guard_t s_guard;
static uint32_t s_ai_id;
static uint8_t *s_last_ai_frame;
static int64_t s_started_us;
static uint32_t s_last_elapsed_ms;

const char *frame_stage_name(frame_stage_t stage)
{
    static const char *const names[]={"idle","queued","connecting","generating","downloading",
        "converting","displaying","succeeded","failed","receiving"};
    return (unsigned)stage<sizeof(names)/sizeof(names[0])?names[stage]:"unknown";
}
static bool configured(void)
{
    size_t ssid=strlen(APP_WIFI_SSID),pass=strlen(APP_WIFI_PASSWORD),token=strlen(APP_MODELSCOPE_TOKEN);
    return ssid>0&&ssid<=32&&pass<=63&&(!pass||pass>=8)&&token>0&&token<=512&&
        !strpbrk(APP_MODELSCOPE_TOKEN,"\r\n")&&strlen(APP_AI_MODEL)>0&&strlen(APP_AI_MODEL)<=128;
}
static void stage(frame_stage_t value)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);s_status.stage=value;xSemaphoreGive(s_lock);
    ESP_LOGI(TAG,"state: %s",frame_stage_name(value));
}
static esp_err_t generate_frame(const char *prompt,uint8_t **frame)
{
    char *url=NULL;uint8_t *compressed=NULL,*rgb=NULL;size_t length=0;int w=0,h=0;
    stage(FRAME_CONNECTING);
    esp_err_t err=network_service_prepare();if(err!=ESP_OK)goto done;
    stage(FRAME_GENERATING);err=modelscope_generate_image(prompt,&url);if(err!=ESP_OK)goto done;
    stage(FRAME_DOWNLOADING);err=modelscope_download_image(url,&compressed,&length);if(err!=ESP_OK)goto done;
    err=network_service_suspend();if(err!=ESP_OK)goto done;
    stage(FRAME_CONVERTING);err=image_decode_rgb(compressed,length,&rgb,&w,&h);
    free(compressed);compressed=NULL;if(err!=ESP_OK)goto done;
    *frame=heap_caps_malloc(EPD_FB_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!*frame){err=ESP_ERR_NO_MEM;goto done;}
    err=image_rgb_to_frame(rgb,(size_t)w*h*3,w,h,true,*frame,EPD_FB_SIZE);
done:
    free(url);free(compressed);free(rgb);return err;
}
static void process_job(frame_job_t *job)
{
    esp_err_t err=ESP_OK;
    bool displayed=false,from_ai=job->frame==NULL;
    if(!job->frame)err=generate_frame(job->prompt,&job->frame);
    esp_err_t radio=network_service_suspend();
    if(err==ESP_OK)err=radio;
    if(err==ESP_OK) {
        stage(FRAME_DISPLAYING);
        /* Allow the COMMIT ATT response to leave before closing the connection. */
        vTaskDelay(pdMS_TO_TICKS(250));
        if(s_guard)err=s_guard(true);
        if(err==ESP_OK) {
            err=epaper_display_show(job->frame,EPD_FB_SIZE,job->mode);
            displayed=err==ESP_OK;
        }
    }
    xSemaphoreTake(s_lock,portMAX_DELAY);
    frame_stage_t failed_stage=s_status.stage;
    /* Keep the frame the panel actually showed for the web preview readback. */
    if(displayed&&from_ai&&s_last_ai_frame) {
        memcpy(s_last_ai_frame,job->frame,EPD_FB_SIZE);
        s_status.last_ai_job_id=job->job_id;s_status.has_ai_preview=true;
    }
    s_status.stage=err==ESP_OK?FRAME_SUCCEEDED:FRAME_FAILED;
    s_status.failure_stage=err==ESP_OK?FRAME_IDLE:failed_stage;
    s_status.last_error=err;
    if(displayed)++s_status.displayed_count;
    s_last_elapsed_ms=s_started_us?(uint32_t)((esp_timer_get_time()-s_started_us)/1000):0;
    /* Keep ownership until radio is restored, so a new task cannot race resume. */
    xSemaphoreGive(s_lock);
    free(job->frame);job->frame=NULL;
    if(s_guard) {
        radio=s_guard(false);
        if(radio!=ESP_OK) {
            xSemaphoreTake(s_lock,portMAX_DELAY);
            s_status.last_error=radio;s_status.stage=FRAME_FAILED;
            s_status.failure_stage=FRAME_DISPLAYING;
            xSemaphoreGive(s_lock);
            ESP_LOGE(TAG,"BLE resume failed: %s",esp_err_to_name(radio));
            err=radio;
        }
    }
    xSemaphoreTake(s_lock,portMAX_DELAY);s_status.busy=false;xSemaphoreGive(s_lock);
    if(err==ESP_OK)ESP_LOGI(TAG,"%s (job %lu)",from_ai?"image generation and display completed":"BLE image display completed",(unsigned long)job->job_id);
    else ESP_LOGE(TAG,"%s job %lu failed at %s: %s",from_ai?"AI":"BLE",(unsigned long)job->job_id,frame_stage_name(failed_stage),esp_err_to_name(err));
}
static void worker(void *arg)
{
    (void)arg;frame_job_t job;
    for(;;)if(xQueueReceive(s_jobs,&job,portMAX_DELAY)==pdTRUE)process_job(&job);
}
esp_err_t frame_service_start(void)
{
    if(s_lock||s_jobs)return ESP_ERR_INVALID_STATE;
    s_lock=xSemaphoreCreateMutex();s_jobs=xQueueCreate(1,sizeof(frame_job_t));
    if(!s_lock||!s_jobs)goto fail;
    /* Preview is optional; the BLE layer reports it as unavailable if absent. */
    s_last_ai_frame=heap_caps_malloc(EPD_FB_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_last_ai_frame)ESP_LOGW(TAG,"AI preview buffer unavailable");
    s_status.configured=configured();
    if(xTaskCreate(worker,"frame_job",24576,NULL,5,NULL)!=pdPASS)goto fail;
    return ESP_OK;
fail:
    if(s_jobs)vQueueDelete(s_jobs);
    if(s_lock)vSemaphoreDelete(s_lock);
    if(s_last_ai_frame){free(s_last_ai_frame);s_last_ai_frame=NULL;}
    s_jobs=NULL;s_lock=NULL;return ESP_ERR_NO_MEM;
}
esp_err_t frame_service_submit_ai_id(uint32_t job_id,const char *prompt)
{
    if(!job_id||!prompt||!*prompt||strlen(prompt)>APP_AI_MAX_PROMPT)return ESP_ERR_INVALID_ARG;
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    frame_job_t job={.mode=APP_AI_FAST_REFRESH?EPD_MODE_FAST:EPD_MODE_FULL,.job_id=job_id};
    memcpy(job.prompt,prompt,strlen(prompt));
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(s_status.busy||!s_status.configured){xSemaphoreGive(s_lock);return ESP_ERR_INVALID_STATE;}
    frame_status_t previous=s_status;
    uint32_t previous_elapsed=s_last_elapsed_ms;
    s_status.busy=true;s_status.source=FRAME_SOURCE_AI;s_status.stage=FRAME_QUEUED;
    s_status.failure_stage=FRAME_IDLE;s_status.job_id=job_id;s_status.last_error=ESP_OK;
    if(xQueueSend(s_jobs,&job,0)!=pdTRUE){
        s_status=previous;s_last_elapsed_ms=previous_elapsed;xSemaphoreGive(s_lock);return ESP_FAIL;
    }
    s_started_us=esp_timer_get_time();s_last_elapsed_ms=0;
    xSemaphoreGive(s_lock);return ESP_OK;
}
esp_err_t frame_service_submit_ai(const char *prompt)
{
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    uint32_t job_id;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    job_id=++s_ai_id;if(!job_id)job_id=++s_ai_id;
    xSemaphoreGive(s_lock);
    return frame_service_submit_ai_id(job_id,prompt);
}
esp_err_t frame_service_reserve(uint32_t id)
{
    if(!id)return ESP_ERR_INVALID_ARG;
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(s_status.busy){xSemaphoreGive(s_lock);return ESP_ERR_INVALID_STATE;}
    s_status.busy=true;s_status.source=FRAME_SOURCE_BLE;s_status.stage=FRAME_RECEIVING;
    s_status.failure_stage=FRAME_IDLE;s_status.job_id=id;s_status.last_error=ESP_OK;
    s_started_us=esp_timer_get_time();s_last_elapsed_ms=0;
    xSemaphoreGive(s_lock);return ESP_OK;
}
static bool receiving(uint32_t id)
{return s_status.busy&&s_status.source==FRAME_SOURCE_BLE&&s_status.stage==FRAME_RECEIVING&&s_status.job_id==id;}
esp_err_t frame_service_commit(uint32_t id,const uint8_t *frame,size_t length,epd_mode_t mode)
{
    if(!frame||length!=EPD_FB_SIZE||(mode!=EPD_MODE_FULL&&mode!=EPD_MODE_FAST))return ESP_ERR_INVALID_ARG;
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(!receiving(id)){xSemaphoreGive(s_lock);return ESP_ERR_INVALID_STATE;}
    frame_job_t job={.mode=mode,.job_id=id};
    job.frame=heap_caps_malloc(EPD_FB_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!job.frame){xSemaphoreGive(s_lock);return ESP_ERR_NO_MEM;}
    memcpy(job.frame,frame,length);s_status.stage=FRAME_QUEUED;
    if(xQueueSend(s_jobs,&job,0)!=pdTRUE){free(job.frame);s_status.stage=FRAME_RECEIVING;xSemaphoreGive(s_lock);return ESP_FAIL;}
    xSemaphoreGive(s_lock);return ESP_OK;
}
esp_err_t frame_service_cancel(uint32_t id,esp_err_t reason)
{
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(!receiving(id)){xSemaphoreGive(s_lock);return ESP_ERR_INVALID_STATE;}
    s_status.busy=false;s_status.stage=FRAME_FAILED;s_status.failure_stage=FRAME_RECEIVING;
    s_status.last_error=reason;
    xSemaphoreGive(s_lock);return ESP_OK;
}
void frame_service_status(frame_status_t *status)
{
    if(!status)return;
    if(!s_lock){memset(status,0,sizeof(*status));return;}
    xSemaphoreTake(s_lock,portMAX_DELAY);*status=s_status;
    if(status->busy&&s_started_us)
        status->elapsed_ms=(uint32_t)((esp_timer_get_time()-s_started_us)/1000);
    else
        status->elapsed_ms=s_last_elapsed_ms;
    xSemaphoreGive(s_lock);
}
esp_err_t frame_service_copy_last_ai_frame(uint32_t job_id,uint8_t *frame)
{
    if(!frame)return ESP_ERR_INVALID_ARG;
    if(!s_lock)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(!s_last_ai_frame||!s_status.has_ai_preview||
       (job_id&&job_id!=s_status.last_ai_job_id)) {
        xSemaphoreGive(s_lock);return ESP_ERR_INVALID_STATE;
    }
    memcpy(frame,s_last_ai_frame,EPD_FB_SIZE);
    xSemaphoreGive(s_lock);return ESP_OK;
}
void frame_service_set_radio_guard(frame_radio_guard_t guard)
{s_guard=guard; /* Startup only, before any job is submitted. */}
