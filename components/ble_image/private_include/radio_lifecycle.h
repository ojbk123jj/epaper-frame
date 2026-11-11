#pragma once
typedef enum { IMAGE_RADIO_RUNNING, IMAGE_RADIO_OFF, IMAGE_RADIO_BROKEN } image_radio_state_t;
typedef struct { image_radio_state_t state; } image_radio_t;
typedef struct { int (*stop_host)(void); int (*deinit_controller)(void); int (*start)(void); } image_radio_ops_t;
int image_radio_pause(image_radio_t *radio,const image_radio_ops_t *ops);
int image_radio_resume(image_radio_t *radio,const image_radio_ops_t *ops);
