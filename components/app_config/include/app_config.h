#pragma once

/* Credentials stay in the ignored local header, outside version control. */
#if !defined(APP_CONFIG_NO_LOCAL) && __has_include("local_config.h")
#include "local_config.h"
#endif

#ifndef APP_WIFI_SSID
#define APP_WIFI_SSID ""
#endif
#ifndef APP_WIFI_PASSWORD
#define APP_WIFI_PASSWORD ""
#endif
#ifndef APP_MODELSCOPE_TOKEN
#define APP_MODELSCOPE_TOKEN ""
#endif
#ifndef APP_AI_MODEL
#define APP_AI_MODEL "Qwen/Qwen-Image-2.1-Turbo"
#endif
#ifndef APP_AI_SIZE
#define APP_AI_SIZE "768x576"
#endif
#ifndef APP_AI_PROMPT
#define APP_AI_PROMPT "黑白红黄四色插画，一只坐在窗边的猫，简洁构图，清晰轮廓，大块色彩，无文字，横向4:3画面"
#endif
#ifndef APP_AI_FAST_REFRESH
#define APP_AI_FAST_REFRESH 0
#endif

#ifndef APP_AI_TIMEOUT_MS
#define APP_AI_TIMEOUT_MS 600000U
#endif
#define APP_AI_MAX_PROMPT 1024U
#define APP_AI_MAX_IMAGE_BYTES (2U * 1024U * 1024U)
