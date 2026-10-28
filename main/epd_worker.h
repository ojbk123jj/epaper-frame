#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "epd_gdem042f86.h"
#include "epd_patterns.h"

typedef struct {
    epd_pattern_id_t pattern;
    epd_mode_t mode;
    epd_color_t solid; /* 仅 EPD_PATTERN_SOLID 使用 */
    bool bench;        /* true: 同图案先 full 再 fast */
} epd_job_t;

esp_err_t epd_worker_start(void);
epd_handle_t epd_worker_handle(void);

/* 最新命令覆盖尚未开始的旧命令 */
esp_err_t epd_worker_submit(const epd_job_t *job);

/* 提交并等待完成（自动序列使用） */
esp_err_t epd_worker_submit_wait(const epd_job_t *job, uint32_t timeout_ms);

void epd_worker_autorun(void);
