# M5Stack StopWatch C152 hardware notes

This is an experimental **C152** port, not a StickC / StickS3 firmware. No physical device was attached for validation.

## Sources

- [Official product documentation](https://docs.m5stack.com/en/core/StopWatch)
- [M5StopWatch-UserDemo](https://github.com/m5stack/M5StopWatch-UserDemo/tree/6b4aa125288b6fe9dca661f10159f6e1e5ee785c), particularly `main/hal/hal_display.cpp`, `hal_ioe.cpp`, `hal_button.cpp`, `hal_pmic.cpp`, and `sdkconfig.defaults`
- [M5IOE1 1.0.8](https://github.com/m5stack/M5IOE1/tree/1.0.8)
- [M5PM1 1.0.6](https://github.com/m5stack/M5PM1/tree/1.0.6)
- [M5GFX 0.2.19](https://github.com/m5stack/M5GFX/tree/0.2.19), CO5300 AMOLED command framing

## Port configuration

| Part | Configuration |
| --- | --- |
| MCU | ESP32-S3; ESP-IDF target `esp32s3` |
| Memory | 16 MB flash, 8 MB octal PSRAM |
| USB | Native USB Serial/JTAG, GPIO19/20; port varies by host |
| A / B | GPIO2 / GPIO1, active low with pull-up |
| I2C | SDA GPIO47, SCL GPIO48 |
| PMIC | M5PM1 at 0x6e |
| Expander | M5IOE1 at 0x4f, with vendor-compatible 0x6f fallback |
| Display | CO5300 AMOLED, 468×466 visible area, X offset 6 |
| QSPI | CS39, CLK40, D0=41, D1=42, D2=46, D3=45; conservative 40 MHz |
| Speaker gate | GPIO14 driven low; audio intentionally disabled |

M5IOE1 labels are one-based: IO8 is bit7 (LCD rail), IO5 is bit4 (OLED reset). The port initializes output values before enabling output direction and preserves unrelated register bits. It disables leftover motor / LCD / amplifier PWM and leaves motor/audio off. It does not configure battery charging currents or low-voltage protection.

B is shared with the upstream SDK pairing/reset handler. Short release records a lap only while timing and outside pairing confirmation. Holding approximately 1.5 seconds resets the timer; continuing to five seconds invokes SDK setup reset/unpair. Release promptly after the timer reset. A operates start/pause/resume. GPIO0 is not a normal control in this port.

The driver sends synchronous DMA stripes, waits for each transaction, and reuses the buffer only after completion. This is a simple status screen, not a full Muse voice UI. Touch, audio, haptics, battery measurement, sleep, and factory-firmware power lifecycle are not implemented. Display timing, orientation, rail behavior, and power consumption require bench validation. Do not leave this experimental firmware running unattended.
