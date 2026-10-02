// SPDX-License-Identifier: MIT
// Display and expander initialization adapted from M5Stack official examples.
// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
/*
 * M5Stack StopWatch C152 minimal status display / focus controls.
 * Hardware-derived portions: Copyright (c) 2026 M5Stack Technology CO LTD.
 *
 * Hardware facts and initialization sequence follow M5Stack's official sources:
 * https://github.com/m5stack/M5StopWatch-UserDemo (main/hal/hal_{display,ioe,button,pmic}.cpp)
 * https://github.com/m5stack/M5IOE1/tree/1.0.8 (src/M5IOE1.h and .cpp)
 * https://github.com/m5stack/M5PM1/tree/1.0.6 (src/M5PM1.h and .cpp)
 * https://github.com/m5stack/M5GFX/tree/0.2.19 (src/lgfx/v1/panel/Panel_AMOLED.cpp)
 *
 * This is a new, small ESP-IDF implementation, not a copy of those drivers.
 * No LVGL or M5Unified is required. SPI writes complete synchronously before
 * the DMA stripe is reused. Every display window has even origin and size.
 * Touch, audio, battery estimation and low-power operation are intentionally
 * absent from this first port. Blue B also confirms SDK pairing; a five-second hold resets SDK setup.
 */
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "focus.h"
#include "link_pairing.h"
#include "led_status.h"
#include "pixel_font.h"

#define LCD_W 468
#define LCD_H 466
#define LCD_OFFSET_X 6
#define STRIPE_H 16
#define LCD_HOST SPI2_HOST
#define BUTTON_A GPIO_NUM_2
#define BUTTON_B GPIO_NUM_1
#define I2C_SDA GPIO_NUM_47
#define I2C_SCL GPIO_NUM_48

/* PYG labels are ONE-based: M5IOE1_PIN_8 equals bit 7, not bit 8. */
#define IOE_MUX       (1u << 0)
#define IOE_AUDIO     (1u << 2)
#define IOE_TOUCH_RST (1u << 3)
#define IOE_OLED_RST  (1u << 4)
#define IOE_LCD_EN    (1u << 7)
#define IOE_MOTOR     (1u << 8)
#define IOE_SPEAKER   (1u << 9)
#define IOE_USED (IOE_MUX | IOE_AUDIO | IOE_TOUCH_RST | IOE_OLED_RST | IOE_LCD_EN | IOE_MOTOR | IOE_SPEAKER)

static const char *TAG = "stopwatch.board";
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_ioe, s_pmic;
static spi_device_handle_t s_lcd;
static uint16_t *s_stripe;
static int s_band_y, s_band_h;
static bool s_ready;
static portMUX_TYPE s_status_mux = portMUX_INITIALIZER_UNLOCKED;
static led_state_t s_state = LED_STATE_BOOT;
static char s_title[33] = "MUSE FOCUS";

static esp_err_t read_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t *data, size_t n)
{
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < 3; ++i) {
        err = i2c_master_transmit_receive(dev, &reg, 1, data, n, 100);
        if (err == ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return err;
}

static esp_err_t write_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t value, size_t n)
{
    uint8_t bytes[] = {reg, value & 255, value >> 8};
    esp_err_t err = ESP_FAIL;
    if (n != 1 && n != 2) return ESP_ERR_INVALID_ARG;
    for (int i = 0; i < 3; ++i) {
        err = i2c_master_transmit(dev, bytes, n + 1, 100);
        if (err == ESP_OK) return err;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return err;
}

/* Both M5 devices use little-endian 16-bit registers. Preserve unrelated pins. */
static esp_err_t update_reg(i2c_master_dev_handle_t dev, uint8_t reg, uint16_t mask, uint16_t value, size_t n)
{
    uint8_t bytes[2] = {0};
    ESP_RETURN_ON_ERROR(read_reg(dev, reg, bytes, n), TAG, "read 0x%02x", reg);
    uint16_t old = bytes[0] | ((uint16_t)bytes[1] << 8);
    uint16_t next = (old & ~mask) | (value & mask);
    ESP_RETURN_ON_ERROR(write_reg(dev, reg, next, n), TAG, "write 0x%02x", reg);
    ESP_RETURN_ON_ERROR(read_reg(dev, reg, bytes, n), TAG, "verify 0x%02x", reg);
    uint16_t actual = bytes[0] | ((uint16_t)bytes[1] << 8);
    return (actual & mask) == (next & mask) ? ESP_OK : ESP_FAIL;
}

static esp_err_t open_i2c_device(uint8_t address, i2c_master_dev_handle_t *result)
{
    /* The devices can retain either speed after ESP resets. Try both, like
     * the vendor drivers. A first read wakes a sleeping device. */
    const uint32_t speeds[] = {100000, 400000};
    for (unsigned i = 0; i < sizeof(speeds) / sizeof(speeds[0]); ++i) {
        const i2c_device_config_t cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = address,
            .scl_speed_hz = speeds[i],
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &cfg, result), TAG, "I2C device");
        uint8_t id[3];
        esp_err_t probe = read_reg(*result, 0, id, sizeof(id));
        if (probe != ESP_OK && i == 0) {
            /* Match the vendor's bounded cold-start wake retry. */
            vTaskDelay(pdMS_TO_TICKS(800));
            probe = read_reg(*result, 0, id, sizeof(id));
        }
        if (probe == ESP_OK) {
            ESP_LOGI(TAG, "I2C 0x%02x at %lu Hz, ID %02x/%02x/%02x", address,
                     (unsigned long)speeds[i], id[0], id[1], id[2]);
            return ESP_OK;
        }
        i2c_master_bus_rm_device(*result);
        *result = NULL;
    }
    return ESP_ERR_NOT_FOUND;
}

static esp_err_t board_power_init(void)
{
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&cfg, &s_bus), TAG, "I2C bus");
    ESP_RETURN_ON_ERROR(open_i2c_device(0x6e, &s_pmic), TAG, "M5PM1 missing");
    /* M5PM1 I2C_CFG.sleep=0 and WDT_CNT=0, as in the official demo. Do not
     * alter charge-current, low-voltage protection, or power-off behavior. */
    ESP_RETURN_ON_ERROR(update_reg(s_pmic, 0x09, 0x0f, 0, 1), TAG, "PMIC stay awake");
    ESP_RETURN_ON_ERROR(update_reg(s_pmic, 0x0a, 0xff, 0, 1), TAG, "PMIC watchdog off");

    esp_err_t ioe_err = open_i2c_device(0x4f, &s_ioe);
    if (ioe_err != ESP_OK) ioe_err = open_i2c_device(0x6f, &s_ioe);
    ESP_RETURN_ON_ERROR(ioe_err, TAG, "M5IOE1 missing");
    ESP_RETURN_ON_ERROR(update_reg(s_ioe, 0x23, 0x0f, 0, 1), TAG, "IOE stay awake");
    /* Disable PWM on IO9 (motor), IO8 (LCD power), IO10 (amp), which may be
     * left enabled by the previous firmware. Preserve other channels. */
    const uint8_t pwm_high_regs[] = {0x1c, 0x1e, 0x22};
    for (unsigned i = 0; i < sizeof(pwm_high_regs); ++i) {
        ESP_RETURN_ON_ERROR(update_reg(s_ioe, pwm_high_regs[i], 0x80, 0, 1), TAG, "PWM off");
    }
    ESP_RETURN_ON_ERROR(update_reg(s_ioe, 0x05, IOE_USED, IOE_LCD_EN | IOE_TOUCH_RST, 2), TAG, "IOE outputs");
    ESP_RETURN_ON_ERROR(update_reg(s_ioe, 0x09, IOE_USED, 0, 2), TAG, "IOE pullups off");
    ESP_RETURN_ON_ERROR(update_reg(s_ioe, 0x0b, IOE_USED, 0, 2), TAG, "IOE pulldowns off");
    ESP_RETURN_ON_ERROR(update_reg(s_ioe, 0x13, IOE_USED, 0, 2), TAG, "IOE push pull");
    ESP_RETURN_ON_ERROR(update_reg(s_ioe, 0x03, IOE_USED, IOE_USED, 2), TAG, "IOE direction");
    /* GPIO14 is the additional PA gate configured low by the vendor demo. */
    ESP_RETURN_ON_ERROR(gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT), TAG, "speaker gate");
    ESP_RETURN_ON_ERROR(gpio_set_level(GPIO_NUM_14, 0), TAG, "speaker disabled");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(update_reg(s_ioe, 0x05, IOE_OLED_RST, IOE_OLED_RST, 2), TAG, "OLED reset release");
    vTaskDelay(pdMS_TO_TICKS(150));
    return ESP_OK;
}

/* CO5300 command frames are 02 00 register 00, in one data lane. Pixel
 * frames are 32 00 2C 00, followed by four-lane RGB565, high byte first. */
static esp_err_t lcd_command(uint8_t command, const uint8_t *data, size_t n)
{
    spi_transaction_t t = {
        .cmd = 0x02,
        .addr = (uint32_t)command << 8,
        .length = n * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_lcd, &t);
}

static esp_err_t panel_init(void)
{
    const spi_bus_config_t bus = {
        .sclk_io_num = GPIO_NUM_40,
        .mosi_io_num = GPIO_NUM_41,
        .miso_io_num = GPIO_NUM_42,
        .quadwp_io_num = GPIO_NUM_46,
        .quadhd_io_num = GPIO_NUM_45,
        .max_transfer_sz = LCD_W * STRIPE_H * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "QSPI bus");
    const spi_device_interface_config_t device = {
        .command_bits = 8,
        .address_bits = 24,
        .clock_speed_hz = 40 * 1000 * 1000, /* conservative vs vendor's 80 MHz */
        .mode = 0,
        .spics_io_num = GPIO_NUM_39,
        .queue_size = 1,
        .flags = SPI_DEVICE_HALFDUPLEX,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(LCD_HOST, &device, &s_lcd), TAG, "CO5300");
    ESP_RETURN_ON_ERROR(lcd_command(0x11, NULL, 0), TAG, "sleep out");
    vTaskDelay(pdMS_TO_TICKS(150));
    /* Same visible window and initialization as hal_display.cpp; explicitly
     * select RGB565 as well. No TE synchronization in this minimal port. */
    ESP_RETURN_ON_ERROR(lcd_command(0xc4, (const uint8_t[]){0x80}, 1), TAG, "CO5300 C4");
    ESP_RETURN_ON_ERROR(lcd_command(0x35, (const uint8_t[]){0x80}, 1), TAG, "TE mode");
    ESP_RETURN_ON_ERROR(lcd_command(0x44, (const uint8_t[]){0x01, 0xd2}, 2), TAG, "TE line");
    ESP_RETURN_ON_ERROR(lcd_command(0x53, (const uint8_t[]){0x20}, 1), TAG, "brightness control");
    ESP_RETURN_ON_ERROR(lcd_command(0x20, NULL, 0), TAG, "inversion off");
    ESP_RETURN_ON_ERROR(lcd_command(0x36, (const uint8_t[]){0x00}, 1), TAG, "orientation");
    ESP_RETURN_ON_ERROR(lcd_command(0x3a, (const uint8_t[]){0x55}, 1), TAG, "RGB565");
    ESP_RETURN_ON_ERROR(lcd_command(0x51, (const uint8_t[]){0x80}, 1), TAG, "brightness");
    return lcd_command(0x29, NULL, 0);
}

static esp_err_t flush_band(void)
{
    unsigned xs = LCD_OFFSET_X, xe = xs + LCD_W - 1;
    unsigned ys = s_band_y, ye = ys + s_band_h - 1;
    uint8_t cols[] = {xs >> 8, xs & 255, xe >> 8, xe & 255};
    uint8_t rows[] = {ys >> 8, ys & 255, ye >> 8, ye & 255};
    ESP_RETURN_ON_ERROR(lcd_command(0x2a, cols, sizeof(cols)), TAG, "columns");
    ESP_RETURN_ON_ERROR(lcd_command(0x2b, rows, sizeof(rows)), TAG, "rows");
    spi_transaction_t t = {
        .cmd = 0x32,
        .addr = 0x002c00,
        .length = LCD_W * s_band_h * 16,
        .tx_buffer = s_stripe,
        .flags = SPI_TRANS_MODE_QIO,
    };
    return spi_device_polling_transmit(s_lcd, &t);
}

/* Colors are byte-swapped once for the panel's big-endian RGB565. */
static uint16_t color(unsigned r, unsigned g, unsigned b)
{
    uint16_t native = ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3);
    return (native >> 8) | (native << 8);
}

static void rect(int x, int y, int w, int h, uint16_t ink)
{
    int left = x < 0 ? 0 : x, right = x + w > LCD_W ? LCD_W : x + w;
    int top = y < s_band_y ? s_band_y : y;
    int bottom = y + h > s_band_y + s_band_h ? s_band_y + s_band_h : y + h;
    for (int yy = top; yy < bottom; ++yy) {
        for (int xx = left; xx < right; ++xx) s_stripe[(yy - s_band_y) * LCD_W + xx] = ink;
    }
}

static void text_center(const char *text, int y, int scale, uint16_t ink)
{
    int n = (int)strlen(text);
    int x = (LCD_W - (n * 6 - 1) * scale) / 2;
    for (int i = 0; i < n; ++i) {
        unsigned ch = (unsigned char)text[i];
        if (ch < PIXEL_FONT_FIRST || ch > PIXEL_FONT_LAST) ch = '?';
        for (int col = 0; col < 5; ++col) {
            uint8_t bits = pixel_font[ch - PIXEL_FONT_FIRST][col];
            for (int row = 0; row < 8; ++row) {
                if (bits & (1u << row)) rect(x + (i * 6 + col) * scale, y + row * scale, scale, scale, ink);
            }
        }
    }
}

static const char *status_label(led_state_t state, uint16_t *ink)
{
    *ink = color(83, 158, 255);
    switch (state) {
    case LED_STATE_BOOT: return "STARTING MUSE";
    case LED_STATE_SETUP_IDLE:
    case LED_STATE_BLE_ADVERTISING:
        *ink = color(255, 159, 67); return "PAIR IN MUSE APP";
    case LED_STATE_BLE_CONNECTED: return "PHONE CONNECTED";
    case LED_STATE_PAIRING_CONFIRM_REQUIRED: return "TAP BLUE B TO PAIR";
    case LED_STATE_WIFI_CONNECTING: return "CONNECTING WI-FI";
    case LED_STATE_WIFI_CONNECTED: return "CONNECTING MUSE";
    case LED_STATE_AUTH_OK: return "AUTHENTICATED";
    case LED_STATE_VM_OK:
    case LED_STATE_WS_CONNECTED:
        *ink = color(72, 218, 157); return "MUSE CONNECTED";
    case LED_STATE_VM_SWITCHING:
    case LED_STATE_WS_DISCONNECTED:
        *ink = color(255, 205, 80); return "MUSE RECONNECTING";
    case LED_STATE_UNPAIRED:
        *ink = color(190, 133, 255); return "PAIR IN MUSE APP";
    case LED_STATE_ERROR:
        *ink = color(255, 100, 100); return "MUSE LINK ERROR";
    default: return "MUSE";
    }
}

static esp_err_t render(void)
{
    focus_snapshot_t f = focus_snapshot();
    led_state_t state;
    char title[sizeof(s_title)];
    portENTER_CRITICAL(&s_status_mux);
    state = s_state;
    memcpy(title, s_title, sizeof(title));
    title[25] = '\0'; /* Keep text inside the round panel. */
    portEXIT_CRITICAL(&s_status_mux);
    uint16_t status_ink;
    const char *link = status_label(state, &status_ink);
    uint16_t white = color(232, 236, 242), dim = color(113, 126, 150);
    uint16_t accent = f.complete ? color(72, 218, 157) : color(109, 190, 255);
    uint64_t ms = f.target_ms ? f.remaining_ms : f.elapsed_ms;
    uint64_t seconds = f.target_ms ? (ms + 999) / 1000 : ms / 1000;
    /* The core's maximum duration fits in hours, and a long-running stopwatch
     * is capped visually to 99:59:59 rather than overflowing the display. */
    if (seconds > 359999) seconds = 359999;
    char time_text[16], lap_text[24];
    if (seconds >= 3600) {
        snprintf(time_text, sizeof(time_text), "%02u:%02u:%02u", (unsigned)(seconds / 3600),
                 (unsigned)((seconds / 60) % 60), (unsigned)(seconds % 60));
    } else {
        snprintf(time_text, sizeof(time_text), "%02u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
    }
    snprintf(lap_text, sizeof(lap_text), "LAPS %lu", (unsigned long)f.laps);
    int progress = 0;
    if (f.target_ms) progress = f.elapsed_ms >= f.target_ms ? 300 : (int)(f.elapsed_ms * 300 / f.target_ms);
    const char *mode = f.complete ? "SESSION COMPLETE" : f.running ? "IN FOCUS" : f.elapsed_ms ? "PAUSED" : "READY";
    for (s_band_y = 0; s_band_y < LCD_H; s_band_y += STRIPE_H) {
        s_band_h = LCD_H - s_band_y < STRIPE_H ? LCD_H - s_band_y : STRIPE_H;
        memset(s_stripe, 0, LCD_W * s_band_h * sizeof(*s_stripe));
        text_center(title, 58, strlen(title) > 16 ? 2 : 3, dim);
        text_center(f.target_ms ? "FOCUS TIMER" : "STOPWATCH", 108, 2, accent);
        text_center(time_text, 154, strlen(time_text) > 5 ? 8 : 11, white);
        text_center(mode, 260, 3, accent);
        rect(84, 305, 300, 6, color(28, 35, 48));
        if (f.target_ms) rect(84, 305, progress, 6, accent);
        text_center(lap_text, 330, 2, dim);
        text_center(link, 363, 2, status_ink);
        text_center("A START/PAUSE  B LAP", 391, 2, color(255, 200, 64));
        text_center("B HOLD 1.5S RESET", 415, 2, color(96, 163, 255));
        esp_err_t err = flush_band();
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}

typedef struct {
    gpio_num_t gpio;
    bool sample, pressed, held, suppressed;
    uint64_t changed_ms, pressed_ms;
} key_t;

static bool pairing_pending(void)
{
    portENTER_CRITICAL(&s_status_mux);
    bool pending = s_state == LED_STATE_PAIRING_CONFIRM_REQUIRED;
    portEXIT_CRITICAL(&s_status_mux);
    return pending;
}

static void poll_key(key_t *key, uint64_t now)
{
    bool down = gpio_get_level(key->gpio) == 0;
    if (down != key->sample) {
        key->sample = down;
        key->changed_ms = now;
    }
    if (down != key->pressed && now - key->changed_ms >= 30) {
        key->pressed = down;
        if (down) {
            key->pressed_ms = now;
            key->hold_handled = false;
            key->suppress = link_pairing_confirmation_required();
            if (key->gpio == BUTTON_A) focus_toggle();
        } else if (key->gpio == BUTTON_B && !key->hold_handled &&
                   !key->suppress && !link_pairing_confirmation_required()) {
            focus_lap();
        }
    }
    if (key->gpio == BUTTON_B && key->pressed && !key->hold_handled &&
        now - key->pressed_ms >= 1500) {
        key->hold_handled = true;
        if (!key->suppress && !link_pairing_confirmation_required()) focus_reset();
    }
}

static void board_task(void *unused)
{
    (void)unused;
    /* Do not interpret a held boot-time key as a new press. */
    key_t a = {.gpio = BUTTON_A}, b = {.gpio = BUTTON_B, .hold_handled = true, .suppress = true};
    a.sample = a.pressed = gpio_get_level(BUTTON_A) == 0;
    b.sample = b.pressed = gpio_get_level(BUTTON_B) == 0;
    a.held = a.pressed;
    b.held = b.pressed; /* Ignore an already-held button until its release. */
    uint64_t next_frame = 0;
    unsigned render_failures = 0;
    for (;;) {
        uint64_t now = esp_timer_get_time() / 1000;
        poll_key(&a, now);
        poll_key(&b, now);
        if (now >= next_frame) {
            esp_err_t err = render();
            if (err != ESP_OK && (render_failures++ % 50) == 0) {
                ESP_LOGE(TAG, "display write failed: %s", esp_err_to_name(err));
            }
            next_frame = now + 100;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool led_status_init(void)
{
    if (s_ready) return true;
    esp_err_t err = board_power_init();
    if (err == ESP_OK) err = panel_init();
    const gpio_config_t keys = {
        .pin_bit_mask = (1ULL << BUTTON_A) | (1ULL << BUTTON_B),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (err == ESP_OK) err = gpio_config(&keys);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hardware initialization failed: %s", esp_err_to_name(err));
        return false;
    }
    s_stripe = heap_caps_malloc(LCD_W * STRIPE_H * sizeof(*s_stripe), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!s_stripe) return false;
    if (xTaskCreate(board_task, "focus_display", 4096, NULL, 2, NULL) != pdPASS) {
        heap_caps_free(s_stripe);
        s_stripe = NULL;
        return false;
    }
    s_ready = true;
    ESP_LOGI(TAG, "M5Stack StopWatch C152 ready: CO5300 468x466, A=GPIO2, B=GPIO1; no voice/touch");
    return true;
}

void led_status_set_state(led_state_t state)
{
    portENTER_CRITICAL(&s_status_mux);
    s_state = state;
    portEXIT_CRITICAL(&s_status_mux);
}

void led_status_set_title(const char *title)
{
    char clean[sizeof(s_title)] = {0};
    snprintf(clean, sizeof(clean), "%s", title && title[0] ? title : "MUSE FOCUS");
    /* The compact SDK font is ASCII-only; avoid invalid multi-byte fragments. */
    for (size_t i = 0; clean[i]; ++i) {
        if ((unsigned char)clean[i] < 32 || (unsigned char)clean[i] > 126) clean[i] = '?';
    }
    portENTER_CRITICAL(&s_status_mux);
    memcpy(s_title, clean, sizeof(s_title));
    portEXIT_CRITICAL(&s_status_mux);
}

/* The focus UI owns the display. Image drawing is deliberately not advertised
 * as supported; otherwise display.draw_url would misleadingly return success. */
bool led_status_display_info(int *width, int *height) { (void)width; (void)height; return false; }
int led_status_display_bits(void) { return 0; }
bool led_status_draw_rect(int x, int y, int w, int h, const uint16_t *pixels)
{ (void)x; (void)y; (void)w; (void)h; (void)pixels; return false; }
void led_status_draw_done(void) {}
void led_status_show_animation(void) {}
void led_status_set_voice(led_voice_t voice) { (void)voice; }
void led_status_set_level(float level) { (void)level; }
void led_status_show_volume(int percent) { (void)percent; }
