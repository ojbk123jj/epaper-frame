#include "epd_console.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_console.h"
#include "esp_log.h"

#include "epd_gdem042f86.h"
#include "epd_patterns.h"
#include "epd_worker.h"

static const char *TAG = "console";

static void print_usage(void)
{
    printf("usage:\n");
    printf("  epd info\n");
    printf("  epd solid <black|white|yellow|red> [full|fast]\n");
    printf("  epd colors|checker|bars|grid|photo [full|fast]\n");
    printf("  epd bench [colors|checker|grid|photo]\n");
    printf("  epd clk <hz>\n");
    printf("  epd sleep\n");
}

static bool parse_mode(const char *s, epd_mode_t *out)
{
    if (s == NULL || strcmp(s, "full") == 0) {
        *out = EPD_MODE_FULL;
        return true;
    }
    if (strcmp(s, "fast") == 0) {
        *out = EPD_MODE_FAST;
        return true;
    }
    return false;
}

static bool parse_color(const char *s, epd_color_t *out)
{
    static const struct {
        const char *name;
        epd_color_t color;
    } k_map[] = {
        {"black", EPD_COLOR_BLACK},
        {"white", EPD_COLOR_WHITE},
        {"yellow", EPD_COLOR_YELLOW},
        {"red", EPD_COLOR_RED},
    };
    for (size_t i = 0; i < sizeof(k_map) / sizeof(k_map[0]); i++) {
        if (strcmp(s, k_map[i].name) == 0) {
            *out = k_map[i].color;
            return true;
        }
    }
    return false;
}

static int do_info(void)
{
    epd_handle_t h = epd_worker_handle();
    printf("panel      : GDEM042F86 %dx%d, 2bpp, fb=%d bytes\n",
           EPD_WIDTH, EPD_HEIGHT, EPD_FB_SIZE);
    printf("spi clock  : %d Hz\n", epd_clock_hz(h));
    printf("busy level : %d (1 = idle)\n", epd_busy_level(h));
    printf("framebuffer: %p\n", (void *)epd_fb(h));
    return 0;
}

static int cmd_epd(int argc, char **argv)
{
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *sub = argv[1];
    epd_job_t job = {
        .pattern = EPD_PATTERN_SOLID,
        .mode = EPD_MODE_FULL,
        .solid = EPD_COLOR_WHITE,
        .bench = false,
    };

    if (strcmp(sub, "info") == 0) {
        return do_info();
    }
    if (strcmp(sub, "sleep") == 0) {
        esp_err_t err = epd_sleep(epd_worker_handle());
        printf("sleep: %s\n", esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "clk") == 0) {
        if (argc < 3) {
            printf("current clock: %d Hz\n", epd_clock_hz(epd_worker_handle()));
            return 0;
        }
        int hz = atoi(argv[2]);
        esp_err_t err = epd_set_clock(epd_worker_handle(), hz);
        printf("set clock %d: %s\n", hz, esp_err_to_name(err));
        return err == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "solid") == 0) {
        if (argc < 3 || !parse_color(argv[2], &job.solid)) {
            printf("invalid color\n");
            return 1;
        }
        job.pattern = EPD_PATTERN_SOLID;
        if (argc >= 4 && !parse_mode(argv[3], &job.mode)) {
            printf("invalid mode (full|fast)\n");
            return 1;
        }
        ESP_LOGI(TAG, "enqueue solid %s %s", argv[2], job.mode == EPD_MODE_FAST ? "fast" : "full");
        return epd_worker_submit(&job) == ESP_OK ? 0 : 1;
    }
    if (strcmp(sub, "bench") == 0) {
        epd_pattern_id_t id = EPD_PATTERN_COLORS;
        if (argc >= 3 && !epd_pattern_lookup(argv[2], &id)) {
            printf("unknown pattern\n");
            return 1;
        }
        job.pattern = id;
        job.bench = true;
        printf("bench queued: %s\n", epd_pattern_name(id));
        return epd_worker_submit(&job) == ESP_OK ? 0 : 1;
    }

    epd_pattern_id_t id;
    if (!epd_pattern_lookup(sub, &id)) {
        printf("unknown subcommand: %s\n", sub);
        print_usage();
        return 1;
    }
    job.pattern = id;
    if (argc >= 3 && !parse_mode(argv[2], &job.mode)) {
        printf("invalid mode (full|fast)\n");
        return 1;
    }
    ESP_LOGI(TAG, "enqueue %s %s", sub, job.mode == EPD_MODE_FAST ? "fast" : "full");
    return epd_worker_submit(&job) == ESP_OK ? 0 : 1;
}

esp_err_t epd_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.prompt = "epd>";
    repl_cfg.max_cmdline_length = 128;

    esp_console_dev_uart_config_t uart_cfg = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_console_new_repl_uart(&uart_cfg, &repl_cfg, &repl), TAG, "repl init");

    esp_console_cmd_t cmd = {
        .command = "epd",
        .help = "EPD test commands (run 'epd' for usage)",
        .hint = NULL,
        .func = &cmd_epd,
    };
    ESP_RETURN_ON_ERROR(esp_console_cmd_register(&cmd), TAG, "cmd register");
    esp_console_register_help_command();

    return esp_console_start_repl(repl);
}
