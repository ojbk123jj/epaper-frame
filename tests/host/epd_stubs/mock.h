#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_SIZE 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_NO_MEM 4
#define ESP_ERR_TIMEOUT 5
#define ESP_ERR_NOT_SUPPORTED 6
#define ESP_ERR_INVALID_RESPONSE 7
#define ESP_RETURN_ON_FALSE(x,e,...) do {if(!(x)) return (e);} while(0)
#define ESP_RETURN_ON_ERROR(x,...) do {int e_=(x);if(e_) return e_;} while(0)
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGD(tag,...) ((void)(tag))
#define CONFIG_EPD_PIN_SCK 12
#define CONFIG_EPD_PIN_MOSI 11
#define CONFIG_EPD_PIN_CS 10
#define CONFIG_EPD_PIN_DC 13
#define CONFIG_EPD_PIN_RST 14
#define CONFIG_EPD_PIN_BUSY 21
#define CONFIG_EPD_SPI_CLOCK_HZ 1000000
#define GPIO_IS_VALID_GPIO(n) ((n)>=0 && (n)<49)
#define GPIO_IS_VALID_OUTPUT_GPIO(n) GPIO_IS_VALID_GPIO(n)
#define GPIO_MODE_INPUT_OUTPUT 1
#define GPIO_MODE_INPUT 2
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
int gpio_set_level(int,int);
int gpio_get_level(int);
int gpio_config(const gpio_config_t*);
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 0
typedef void* spi_device_handle_t;
typedef struct {size_t length;const void *tx_buffer;} spi_transaction_t;
typedef struct {int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz;} spi_bus_config_t;
typedef struct {int clock_speed_hz,mode,spics_io_num,queue_size;} spi_device_interface_config_t;
int spi_device_polling_transmit(spi_device_handle_t,spi_transaction_t*);
int spi_bus_initialize(int,const spi_bus_config_t*,int);
int spi_bus_add_device(int,const spi_device_interface_config_t*,spi_device_handle_t*);
int spi_bus_remove_device(spi_device_handle_t);
int spi_bus_free(int);
#define MALLOC_CAP_8BIT 1
#define MALLOC_CAP_SPIRAM 2
#define MALLOC_CAP_DMA 4
static inline void *heap_caps_malloc(size_t n,uint32_t c){(void)c;return malloc(n);}
static inline void *heap_caps_realloc(void *p,size_t n,uint32_t c){(void)c;return realloc(p,n);}
#define pdMS_TO_TICKS(ms) (ms)
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
