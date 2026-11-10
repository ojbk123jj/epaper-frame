#include "ai_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "ai_service.h"
#include "app_config.h"
#include "esp_console.h"

static int command_ai(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "status")) {
        ai_status_t status;
        ai_service_status(&status);
        printf("configured=%s, busy=%s, state=%s, displayed=%" PRIu32 ", last_result=%s\n",
               status.configured ? "yes" : "no", status.busy ? "yes" : "no",
               ai_stage_name(status.stage), status.displayed_count, esp_err_to_name(status.last_error));
        return 0;
    }
    char prompt[APP_AI_MAX_PROMPT + 1];
    size_t used = 0;
    for (int i = 1; i < argc; ++i) {
        size_t n = strlen(argv[i]);
        size_t separator = i > 1 ? 1 : 0;
        if (n + separator > APP_AI_MAX_PROMPT - used) {
            printf("prompt is too long (max %u UTF-8 bytes)\n", (unsigned)APP_AI_MAX_PROMPT);
            return 1;
        }
        if (separator) prompt[used++] = ' ';
        memcpy(prompt + used, argv[i], n); used += n;
    }
    prompt[used] = '\0';
    esp_err_t err = ai_service_submit(argc == 1 ? APP_AI_PROMPT : prompt);
    if (err == ESP_OK) printf("AI image request queued; use 'ai status' for progress\n");
    else printf("request rejected: %s (check local configuration or wait for the current task)\n", esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

esp_err_t ai_console_start(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    config.prompt = "frame>";
    config.max_cmdline_length = APP_AI_MAX_PROMPT + 16;
    config.task_stack_size = 8192;
    config.max_cmdline_args = APP_AI_MAX_PROMPT / 2 + 2;
    esp_console_dev_uart_config_t uart = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    esp_err_t err = esp_console_new_repl_uart(&uart, &config, &repl);
    if (err != ESP_OK) return err;
    esp_console_cmd_t command = {
        .command = "ai",
        .help = "ai [prompt] generates an image; ai status reports progress",
        .func = command_ai,
    };
    err = esp_console_cmd_register(&command);
    if (err != ESP_OK) return err;
    esp_console_register_help_command();
    return esp_console_start_repl(repl);
}
