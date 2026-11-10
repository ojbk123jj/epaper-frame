#include <assert.h>
#include <stdio.h>

#include "mock.h"
#include "../../components/epd_gdem042f86/src/epd_gdem042f86.c"

static int levels[49], commands[256];
static int fault, fail_cmd, fail_data_cmd, suppress_cmd, stuck_cmd, current_cmd;
static int64_t now, busy_from, busy_until, reset_from, standby_ready_at, power_off_done;
static bool sleeping, powered;
static size_t frame_bytes;
static int sleep_entries, reset_releases;
static int64_t last_reset_low_us, last_reset_release_us, case_start;

int64_t esp_timer_get_time(void) { return now; }
void vTaskDelay(unsigned ms) { now += (int64_t)ms * 1000; }

int gpio_set_level(int pin, int value)
{
    if (pin == CONFIG_EPD_PIN_RST) {
        if (value == 0) {
            if (levels[pin] != 0) reset_from = now;
            sleeping = powered = false;
            busy_from = busy_until = 0;
        } else if (levels[pin] == 0) {
            assert(levels[CONFIG_EPD_PIN_CS] == 1);
            assert(now - reset_from >= 20000);
            last_reset_low_us = now - reset_from;
            last_reset_release_us = now;
            ++reset_releases;
            standby_ready_at = now + 50000;
        }
    }
    levels[pin] = value;
    return ESP_OK;
}

int gpio_get_level(int pin)
{
    if (pin != CONFIG_EPD_PIN_BUSY) return levels[pin];
    if (fault == 2) return 0;
    if (fault == 1) return 1;
    return !(now >= busy_from && now < busy_until);
}

int gpio_config(const gpio_config_t *cfg) { (void)cfg; return ESP_OK; }
int spi_bus_initialize(int host, const spi_bus_config_t *cfg, int dma)
{
    (void)host; (void)cfg; (void)dma;
    return ESP_OK;
}
int spi_bus_add_device(int host, const spi_device_interface_config_t *cfg,
                       spi_device_handle_t *out)
{
    (void)host; (void)cfg;
    *out = (void *)1;
    return ESP_OK;
}
int spi_bus_remove_device(spi_device_handle_t h) { (void)h; return ESP_OK; }
int spi_bus_free(int host) { (void)host; return ESP_OK; }

int spi_device_polling_transmit(spi_device_handle_t h, spi_transaction_t *tx)
{
    assert(h != NULL);
    assert(levels[CONFIG_EPD_PIN_RST] == 1);
    assert(levels[CONFIG_EPD_PIN_CS] == 0);
    assert(!sleeping);
    assert(now >= standby_ready_at);
    if (levels[CONFIG_EPD_PIN_DC] != 0) {
        if (current_cmd == fail_data_cmd) return ESP_FAIL;
        if (current_cmd == 0x10) frame_bytes += tx->length / 8;
        if (current_cmd == 0x07) {
            assert(tx->length == 8 && *(const uint8_t *)tx->tx_buffer == 0xa5);
            sleeping = true;
            ++sleep_entries;
        }
        return ESP_OK;
    }

    /* Also reject a command during the delayed onset of a BUSY cycle. */
    assert(now >= busy_until);
    current_cmd = *(const uint8_t *)tx->tx_buffer;
    commands[current_cmd]++;
    if (current_cmd == fail_cmd) return ESP_FAIL;

    if (current_cmd == 0x10) frame_bytes = 0;
    if (current_cmd == 0x12) {
        assert(powered);
        assert(frame_bytes == EPD_FB_SIZE);
    }
    if (current_cmd == 0x04) {
        powered = true;
    }
    if (current_cmd == 0x02) {
        assert(powered);
        powered = false;
    }
    if (current_cmd == 0x07) {
        assert(!powered && power_off_done > 0 && now >= power_off_done);
    }
    if (current_cmd == 0x04 || current_cmd == 0x12 ||
        current_cmd == 0x02 || current_cmd == 0xa5) {
        if (current_cmd != suppress_cmd) {
            busy_from = now + 1000;
            busy_until = current_cmd == stuck_cmd ? INT64_MAX : now + 50000;
            if (current_cmd == 0x02) power_off_done = busy_until;
        }
    }
    return ESP_OK;
}

static void reset_mocks(void)
{
    /* Keep time monotonic across fixtures: the device retains its recovery
     * timestamp. All preceding operations finish with reset asserted. */
    case_start = now;
    busy_from = busy_until = standby_ready_at = power_off_done = 0;
    fault = 0;
    fail_cmd = fail_data_cmd = suppress_cmd = stuck_cmd = current_cmd = -1;
    sleeping = powered = false;
    frame_bytes = 0;
    sleep_entries = reset_releases = 0;
    last_reset_low_us = 0;
    memset(levels, 0, sizeof(levels));
    memset(commands, 0, sizeof(commands));
}

static void expect_refresh_failure(epd_handle_t h, epd_mode_t mode, esp_err_t expected)
{
    uint32_t elapsed = 99;
    assert(epd_refresh(h, mode, &elapsed) == expected);
    assert(elapsed == 0);
    assert(levels[CONFIG_EPD_PIN_RST] == 0);
    int off_commands = commands[0x02];
    int sleep_commands = commands[0x07];
    assert(epd_sleep(h) == ESP_ERR_INVALID_STATE);
    assert(commands[0x02] == off_commands && commands[0x07] == sleep_commands);
    assert(levels[CONFIG_EPD_PIN_CS] == 1);
}

int main(void)
{
    epd_config_t cfg;
    epd_default_config(&cfg);
    epd_handle_t h;
    reset_mocks();
    assert(epd_new(&cfg, &h) == ESP_OK);
    assert(levels[CONFIG_EPD_PIN_RST] == 0);
    assert(epd_sleep(h) == ESP_ERR_INVALID_STATE);
    assert(commands[0x02] == 0 && commands[0x07] == 0);
    /* No time/reset of the model between jobs. Cover consecutive full/fast
     * refreshes and both mode transitions, with completed shutdown each time. */
    const epd_mode_t modes[] = {
        EPD_MODE_FULL, EPD_MODE_FULL, EPD_MODE_FAST,
        EPD_MODE_FAST, EPD_MODE_FULL, EPD_MODE_FAST,
    };
    int64_t previous_shutdown_at = 0;
    for (int i = 0; i < 6; i++) {
        /* A long human pause must count, without adding a full extra cooldown. */
        if (i == 2) vTaskDelay(15000);
        int64_t started_at = now;
        uint32_t elapsed = 0;
        assert(epd_refresh(h, modes[i], &elapsed) == ESP_OK);
        assert(elapsed > 0);
        assert(commands[0x04] == i + 1 && commands[0x12] == i + 1);
        assert(commands[0x02] == i + 1 && commands[0x07] == i + 1);
        assert(now >= power_off_done);
        /* Even immediate consecutive jobs get a fresh, >=1s reset hold. */
        assert(reset_releases == i + 1 && last_reset_low_us >= 1000000);
        if (i > 0) assert(last_reset_release_us - previous_shutdown_at >= 8000000);
        if (i == 2) {
            assert(last_reset_release_us - started_at >= 1000000);
            assert(last_reset_release_us - started_at < 1010000);
        }
        assert(sleep_entries == i + 1 && !powered);
        assert(levels[CONFIG_EPD_PIN_RST] == 0 && levels[CONFIG_EPD_PIN_CS] == 1);
        int64_t done_at = now;
        previous_shutdown_at = done_at;
        assert(epd_sleep(h) == ESP_OK);
        assert(now == done_at && commands[0x02] == i + 1 && commands[0x07] == i + 1);
    }
    assert(commands[0xa5] == 3);

    reset_mocks(); fault = 1;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_ERR_TIMEOUT);
    assert(commands[0x10] == 0 && commands[0x12] == 0 && commands[0x02] == 0);

    reset_mocks(); fault = 2;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_ERR_TIMEOUT);
    assert(commands[0x04] == 0);

    reset_mocks(); fail_cmd = 0x06;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_FAIL);
    assert(commands[0x04] == 0 && commands[0x12] == 0);

    reset_mocks(); fail_cmd = 0x04;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_FAIL);
    assert(commands[0x10] == 0 && commands[0x12] == 0);

    reset_mocks(); stuck_cmd = 0x04;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_ERR_TIMEOUT);
    assert(commands[0x10] == 0 && commands[0x12] == 0);

    reset_mocks(); suppress_cmd = 0xa5;
    expect_refresh_failure(h, EPD_MODE_FAST, ESP_ERR_TIMEOUT);
    assert(commands[0x10] == 0 && commands[0x12] == 0);

    reset_mocks(); fail_cmd = 0x10;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_FAIL);
    assert(commands[0x12] == 0);

    reset_mocks(); fail_data_cmd = 0x10;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_FAIL);
    assert(commands[0x12] == 0 && commands[0x02] == 0);

    reset_mocks(); fail_cmd = 0x12;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_FAIL);
    assert(commands[0x12] == 1 && commands[0x02] == 0);

    reset_mocks(); suppress_cmd = 0x12;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_ERR_TIMEOUT);
    assert(commands[0x12] == 1 && commands[0x02] == 0 && commands[0x07] == 0);
    assert(epd_sleep(h) == ESP_ERR_INVALID_STATE);
    assert(commands[0x02] == 0 && commands[0x07] == 0);

    /* The next attempt recovers through reset without resetting mock state. */
    suppress_cmd = -1;
    int64_t failed_at = now;
    assert(epd_refresh(h, EPD_MODE_FULL, NULL) == ESP_OK);
    assert(commands[0x12] == 2 && commands[0x07] == 1);
    assert(last_reset_release_us - failed_at >= 8000000);

    reset_mocks(); stuck_cmd = 0x12;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_ERR_TIMEOUT);
    assert(now - case_start < 52000000 && commands[0x12] == 1 && commands[0x07] == 0);

    reset_mocks(); stuck_cmd = 0x12;
    expect_refresh_failure(h, EPD_MODE_FAST, ESP_ERR_TIMEOUT);
    assert(now - case_start < 37000000 && commands[0x12] == 1 && commands[0x07] == 0);

    reset_mocks(); fail_cmd = 0x02;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_FAIL);
    assert(commands[0x07] == 0);

    reset_mocks(); suppress_cmd = 0x02;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_ERR_TIMEOUT);
    assert(commands[0x07] == 0);

    reset_mocks(); stuck_cmd = 0x02;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_ERR_TIMEOUT);
    assert(commands[0x07] == 0);

    reset_mocks(); fail_cmd = 0x07;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_FAIL);
    assert(now >= power_off_done);

    reset_mocks(); fail_data_cmd = 0x07;
    expect_refresh_failure(h, EPD_MODE_FULL, ESP_FAIL);
    assert(sleep_entries == 0 && now >= power_off_done);

    assert(epd_refresh(h, (epd_mode_t)99, NULL) == ESP_ERR_INVALID_ARG);
    assert(epd_del(h) == ESP_OK);
    cfg.pin_busy = cfg.pin_cs;
    assert(epd_new(&cfg, &h) == ESP_ERR_INVALID_ARG && h == NULL);
    puts("EPD safety tests passed");
    return 0;
}
