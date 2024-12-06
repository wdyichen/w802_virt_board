#include <math.h>
#include <stdio.h>
#include "wmsdk_config.h"
#include "wm_drv_i2c.h"
#include "wm_drv_gpio.h"
#include "wm_drv_tft_lcd.h"
#include "wm_drv_sdh_spi.h"
#include "wm_drv_adc.h"
#include "wm_drv_eeprom.h"
#include "wm_drv_sdh_sdmmc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wm_netif.h"
#include "wm_cli.h"
#include "lwip/netifapi.h"
#include "emac_opencores.h"
#include "image.h"
#include "fastbee.h"

#define LOG_TAG "virt_board"
#include "wm_log.h"

#define SHT30_ADDRESS                   0x44

#define KEY_PIN_SW1                     WM_GPIO_NUM_0
#define KEY_PIN_SW2                     WM_GPIO_NUM_32
#define KEY_PIN_SW3                     WM_GPIO_NUM_34
#define KEY_PIN_SW4                     WM_GPIO_NUM_37
#define KEY_PIN_SW5                     WM_GPIO_NUM_38

#define KEY_PIN_BEEP                    WM_GPIO_NUM_45

/* divide the image as many blocks, allocate one application buffer to send them one by one
 * this is in order to use less memory for the screen refresh*/
#define LCD_DATA_DRAW_LINE_UNIT        (40)

#define LCD_RGB565_BLACK               0x0000
#define LCD_RGB565_BLUE                0x001F
#define LCD_RGB565_RED                 0xF800
#define LCD_RGB565_GREEN               0x07E0
#define LCD_RGB565_CYAN                0x07FF
#define LCD_RGB565_MAGENTA             0xF81F
#define LCD_RGB565_YELLOW              0xFFE0
#define LCD_RGB565_WHITE               0xFFFF

typedef struct {
    const uint8_t *image_buf;
    uint16_t image_width;
    uint16_t image_high;
    uint16_t image_size;
} image_attr_t;

static uint8_t sht30_calc_crc8(const uint8_t *buf)
{
    uint8_t remainder;
    uint8_t i = 0, j = 0;

    remainder = 0xff;

    for (j = 0; j < 2; j++)
    {
        remainder ^= buf[j];

        for (i = 0; i < 8; i++)
        {
            if (remainder & 0x80)
            {
                remainder = (remainder << 1) ^ 0x31;
            }
            else
            {
                remainder = (remainder << 1);
            }
        }
    }

    return remainder;
}

static void cmd_sht30(int argc, char *argv[])
{
    int ret;
    float temp;
    float humi;
    uint16_t data;
    uint8_t buf[6];
    wm_device_t *dev;
    wm_drv_i2c_config_t config;

    dev = wm_dt_get_device_by_name("i2c");
    if (!(dev && (WM_DEV_ST_INITED == dev->state)))
        dev = wm_drv_i2c_init("i2c");

    if (dev) {
        config.addr = SHT30_ADDRESS;
        config.speed_hz = 400000;

        //sw reset
        buf[0] = 0x30;
        buf[1] = 0xA2;

        ret = wm_drv_i2c_write(dev, &config, &buf[0], 1, &buf[1], 1);
        if (WM_ERR_SUCCESS == ret) {
            vTaskDelay(pdMS_TO_TICKS(100));//500

            //measurement mode
            buf[0] = 0x27;//0x22;
            buf[1] = 0x21;//0x20;
            ret = wm_drv_i2c_write(dev, &config, &buf[0], 1, &buf[1], 1);
        }

        if (WM_ERR_SUCCESS == ret) {
            vTaskDelay(pdMS_TO_TICKS(100));//50

            //PERIODIC_MODE
            buf[0] = 0xE0;
            buf[1] = 0x00;
            ret = wm_drv_i2c_write(dev, &config, &buf[0], 1, &buf[1], 1);

            if (WM_ERR_SUCCESS == ret) {
                ret = wm_drv_i2c_read(dev, &config, NULL, 0, buf, 6);
                if (WM_ERR_SUCCESS == ret) {
                    if ((sht30_calc_crc8(&buf[0]) == buf[2]) &&
                        (sht30_calc_crc8(&buf[3]) == buf[5]))
                    {
                        data = ((uint16_t)buf[0] << 8) | buf[1];
                        temp = -45 + (175 * (float)data / 65535);

                        data = ((uint16_t)buf[3] << 8) | buf[4];
                        humi = (100 * (float)data / 65535);

                        wm_cli_printf("sht30 temp = %.1f, humi = %.1f%%\r\n", temp, humi);
                    }
                }
            }
        }
    }
}
WM_CLI_CMD_DEFINE(sht30, cmd_sht30, sht30 cmd, sht30 -- show temperature and humidity);

static void cmd_at24c256(int argc, char *argv[])
{
    int ret;
    uint16_t offset;
    wm_device_t *dev;

    if (argc != 4)
        return;

    dev = wm_dt_get_device_by_name("i2c");
    if (!(dev && (WM_DEV_ST_INITED == dev->state)))
        dev = wm_drv_i2c_init("i2c");

    if (dev) {
        dev = wm_dt_get_device_by_name("eeprom0");
        if (!(dev && (WM_DEV_ST_INITED == dev->state)))
            dev = wm_drv_eeprom_init("eeprom0");
    }

    if (dev) {
        offset = atoi(argv[2]);

        if (!strcmp("read", argv[1])) {
            int len = atoi(argv[3]);
            char buf[len];
            ret = wm_drv_eeprom_read(dev, offset, buf, len);
            if (ret == WM_ERR_SUCCESS) {
                wm_cli_printf("at24c256 read: %.*s\r\n", len, buf);
                wm_log_dump(WM_LOG_LEVEL_INFO, "at24c256", 16, buf, len);
            } else {
                wm_cli_printf("at24c256 read fail\r\n");
            }
        } else if (!strcmp("write", argv[1])) {
            ret = wm_drv_eeprom_write(dev, offset, argv[3], strlen(argv[3]));
            if (ret == WM_ERR_SUCCESS) {
                wm_cli_printf("at24c256 write success\r\n");
            } else {
                wm_cli_printf("at24c256 write fail\r\n");
            }
        } else {
            return;
        }
    }
}
WM_CLI_CMD_DEFINE(at24c256, cmd_at24c256, at24c256 cmd, at24c256 <read | write> <offset> <len | data> -- read or write eeprom);

static void cmd_sdmmc(int argc, char *argv[])
{
    int ret;
    wm_device_t *dev;

    if (argc < 2)
        return;

    dev = wm_dt_get_device_by_name("sdmmc");
    if (!(dev && (WM_DEV_ST_INITED == dev->state)))
        dev = wm_drv_sdh_sdmmc_init("sdmmc");

    if (dev) {
        if (!strcmp("info", argv[1])) {
            wm_drv_sdh_sdmmc_card_info_t card_info;
            memset(&card_info, 0, sizeof(card_info));
            ret = wm_drv_sdh_sdmmc_get_card_info(dev, &card_info);
            if (WM_ERR_SUCCESS == ret) {
                const char *card_type[4] = {"", "sd", "sdsc", "sdhc"};
                const char *size_unit[] = {"Bytes", "KB", "MB", "GB"};
                int count = 0;
                uint32_t size = card_info.capacity;
                while (1) {
                    if (size / 1024) {
                        size /= 1024;
                        count++;
                    } else {
                        break;
                    }
                }
                wm_cli_printf("type: %s, total size %u%s, block size %u.\r\n", card_type[card_info.type], size, size_unit[count], card_info.block_size);
            }
        } else if (!strcmp("read", argv[1])) {
            if (argc != 4)
                return;
            uint8_t *sdmmc_buf = NULL;
            uint32_t sdmmc_len = 512 * atoi(argv[3]);
            sdmmc_buf = malloc(sdmmc_len);
            if (sdmmc_buf == NULL)
                return;
            memset(sdmmc_buf, 0, sdmmc_len);
            ret = wm_drv_sdh_sdmmc_read_blocks(dev, sdmmc_buf, atoi(argv[2]), atoi(argv[3]));
            if (ret != WM_ERR_SUCCESS) {
                wm_cli_printf("read sdmmc block failed\r\n");
            } else {
                wm_cli_printf("read sdmmc block success\r\n");
                wm_log_dump(WM_LOG_LEVEL_INFO, "sdmmc", 16, sdmmc_buf, sdmmc_len);
            }
            free(sdmmc_buf);
        } else if (!strcmp("write", argv[1])) {
            if (argc != 4)
                return;
            uint8_t *sdmmc_buf = NULL;
            uint32_t sdmmc_len = 512 * atoi(argv[3]);
            sdmmc_buf = malloc(sdmmc_len);
            if (sdmmc_buf == NULL)
                return;
            for (uint32_t i = 0; i < sdmmc_len; i++) {
                sdmmc_buf[i] = i;
            }
            ret = wm_drv_sdh_sdmmc_write_blocks(dev, sdmmc_buf, atoi(argv[2]), atoi(argv[3]));
            if (ret != WM_ERR_SUCCESS) {
                wm_cli_printf("write sdmmc block failed\r\n");
            } else {
                wm_cli_printf("write sdmmc block success\r\n");
                wm_log_dump(WM_LOG_LEVEL_INFO, "sdmmc", 16, sdmmc_buf, sdmmc_len);
            }
            free(sdmmc_buf);
        } else {
            return;
        }
    } else {
        wm_cli_printf("sdmmc init failed, maybe sd card not exist\r\n");
    }
}
WM_CLI_CMD_DEFINE(sdmmc, cmd_sdmmc, sdmmc cmd, sdmmc <read | write | info> [start_block] [num_block] -- info or read or write sdmmc);

static void cmd_ntc(int argc, char *argv[])
{
    int ret;
    float temp;
    int voltage;
    int rt;
    float rp = 100000;
    float t2 = 273.15 + 25;
    float bx = 3950;
    float ka = 273.15;
    wm_device_t *adc_dev = NULL;

    adc_dev = wm_dt_get_device_by_name("adc");
    if (!(adc_dev && (WM_DEV_ST_INITED == adc_dev->state)))
        adc_dev = wm_drv_adc_init("adc");

    if (adc_dev) {
        ret = wm_drv_adc_oneshot(adc_dev, WM_ADC_CHANNEL_2, &voltage);
        if (WM_ERR_SUCCESS == ret) {
            wm_log_debug("voltage %dmv", voltage);

            rt = ((100 * 1000) * voltage) / ((3300) - voltage);//ref_r:100k,ref_v:3.3v
            wm_log_debug("resistacne %u", rt);

            rt = rt * 10;

            //Rt = R EXP(B(1/T1-1/T2))
            //T1=1/(ln(Rt/R)/B+1/T2)
            //T1=1/(log(Rt/R)/B+1/T2)
            temp= 1 / (1 / t2 + log(((float)rt) / rp) / bx) - ka + 0.5;
            wm_cli_printf("ntc temp = %.1f\r\n", temp);
        }
    }
}
WM_CLI_CMD_DEFINE(ntc, cmd_ntc, ntc cmd, ntc -- show temperature);

static int lcd_clean_screen(wm_device_t *dev, uint8_t *buf, uint32_t buf_len, uint16_t bk_color)
{
    wm_lcd_data_desc_t data_desc = { 0 };
    uint16_t width               = 0;
    uint16_t high                = 0;
    int ret                      = WM_ERR_FAILED;
    wm_lcd_capabilitys_t cap     = { 0 };

    wm_drv_tft_lcd_get_capability(dev, &cap);

    width = cap.x_resolution;
    high  = cap.y_resolution;

    for (int x = 0; x < buf_len; x += 2) {
        buf[x]     = (uint8_t)(bk_color >> 8);
        buf[x + 1] = (uint8_t)(bk_color & 0x00FF);
    }

    /* The maximum number of lines for each drawing is LCD_DATA_DRAW_LINE_UNIT */
    for (int i = 0; i < high;) {
        data_desc.x_start  = 0;
        data_desc.x_end    = width - 1;
        data_desc.y_start  = i;
        data_desc.y_end    = (i + LCD_DATA_DRAW_LINE_UNIT > high) ? (high - 1) : (i + LCD_DATA_DRAW_LINE_UNIT - 1);
        data_desc.buf      = buf;
        data_desc.buf_size = (data_desc.y_end - i + 1) * width * WM_CFG_TFT_LCD_PIXEL_WIDTH;

        ret = wm_drv_tft_lcd_draw_bitmap(dev, data_desc);
        if (ret != WM_ERR_SUCCESS) {
            wm_log_error("tft_lcd_draw_bitmap ret=%d", ret);
        }

        i = data_desc.y_end + 1;
    }

    return ret;
}

static int lcd_show_image(wm_device_t *dev, uint8_t *buf, uint32_t buf_len, image_attr_t img)
{
    wm_lcd_data_desc_t data_desc = { 0 };
    uint16_t width               = 0;
    uint16_t high                = 0;
    int ret                      = WM_ERR_FAILED;

    width = img.image_width;
    high  = img.image_high;

    /* The maximum number of lines for each drawing is LCD_DATA_DRAW_LINE_UNIT */
    for (int i = 0; i < high;) {
        data_desc.x_start  = 0;
        data_desc.x_end    = width - 1;
        data_desc.y_start  = i;
        data_desc.y_end    = (i + LCD_DATA_DRAW_LINE_UNIT > high) ? (high - 1) : (i + LCD_DATA_DRAW_LINE_UNIT - 1);
        data_desc.buf      = buf;
        data_desc.buf_size = (data_desc.y_end - i + 1) * width * WM_CFG_TFT_LCD_PIXEL_WIDTH;

        memcpy(data_desc.buf, img.image_buf + (i * width * WM_CFG_TFT_LCD_PIXEL_WIDTH), data_desc.buf_size);
        wm_log_debug("data_desc.buf=%p, size=%d", data_desc.buf, data_desc.buf_size);

        ret = wm_drv_tft_lcd_draw_bitmap(dev, data_desc);
        if (ret != WM_ERR_SUCCESS) {
            wm_log_error("lcd_show_image ret=%d", ret);
        }

        i = data_desc.y_end + 1;
    }

    return ret;
}

static void cmd_lcd(int argc, char *argv[])
{
    int ret = WM_ERR_FAILED;
    wm_device_t *dev    = NULL;
    uint8_t *app_buf    = NULL;
    uint32_t block_size = 0;
    image_attr_t img    = { 0 };
    uint16_t width = 0;
    wm_lcd_capabilitys_t cap = { 0 };
    uint16_t bk_color;

    if (argc != 2)
        return;

    dev = wm_dt_get_device_by_name("sdspi");
    if (!(dev && (WM_DEV_ST_INITED == dev->state)))
        dev = wm_drv_sdh_spi_init("sdspi");

    if (!dev) {
        wm_log_error("init sdspi fail.");
        return;
    }

    dev = wm_dt_get_device_by_name("nv3041a_spi");
    if (!(dev && (WM_DEV_ST_INITED == dev->state))) {
        dev = wm_drv_tft_lcd_init("nv3041a_spi");
        if (dev) {
            ret = wm_drv_tft_lcd_set_backlight(dev, true);
            if (ret != WM_ERR_SUCCESS) {
                wm_log_error("lcd bl set fail.");
            }
        } else {
            wm_log_error("init lcd fail.");
            return;
        }
    }

    if (!strcmp("on", argv[1])) {
        /* turn on the backlight*/
        ret = wm_drv_tft_lcd_set_backlight(dev, true);
        if (ret != WM_ERR_SUCCESS) {
            wm_log_error("lcd bl set on fail.");
        }
    } else if (!strcmp("off", argv[1])) {
        /* turn off the backlight*/
        ret = wm_drv_tft_lcd_set_backlight(dev, false);
        if (ret != WM_ERR_SUCCESS) {
            wm_log_error("lcd bl set off fail.");
        }
    }  else if (strstr("image | red | green | blue | black | white | cyan | magenta | yellow", argv[1])) {
        /* show LCD capability */
        wm_drv_tft_lcd_get_capability(dev, &cap);
        wm_log_debug("LCD x_resolution = %d", cap.x_resolution);
        wm_log_debug("LCD y_resolution = %d", cap.y_resolution);
        wm_log_debug("LCD rotation = %d", cap.rotation);

        //NOTE: when color mode change , the byte width could be adjusted too.
        /* malloc an application buffer to refresh the screen*/
        width = cap.x_resolution;

        block_size = (LCD_DATA_DRAW_LINE_UNIT * width * WM_CFG_TFT_LCD_PIXEL_WIDTH);
        wm_log_debug("DEMO:block_size=%d", block_size);

        app_buf = malloc(block_size);
        if (app_buf == NULL) {
            wm_log_error("mem err");
            return;
        }

        wm_log_debug("wm_lcd_demo show %s background", argv[1]);

        if (!strcmp("image", argv[1])) {
            img.image_buf   = image_hello_world_480x272;//image_bluesky_480x272;
            img.image_width = 480;
            img.image_high  = 272;
            ret = lcd_show_image(dev, app_buf, block_size, img);
            if (ret != WM_ERR_SUCCESS) {
                wm_log_error("lcd_show_image ret=%d", ret);
            }
            free(app_buf);
            return;
        } else if (!strcmp("red", argv[1])) {
            bk_color = LCD_RGB565_RED;
        } else if (!strcmp("green", argv[1])) {
            bk_color = LCD_RGB565_GREEN;
        } else if (!strcmp("blue", argv[1])) {
            bk_color = LCD_RGB565_BLUE;
        } else if (!strcmp("black", argv[1])) {
            bk_color = LCD_RGB565_BLACK;
        } else if (!strcmp("white", argv[1])) {
            bk_color = LCD_RGB565_WHITE;
        } else if (!strcmp("cyan", argv[1])) {
            bk_color = LCD_RGB565_CYAN;
        } else if (!strcmp("magenta", argv[1])) {
            bk_color = LCD_RGB565_MAGENTA;
        } else if (!strcmp("yellow", argv[1])) {
            bk_color = LCD_RGB565_YELLOW;
        } else {
            free(app_buf);
            return;
        }

        ret = lcd_clean_screen(dev, app_buf, block_size, bk_color);
        if (ret != WM_ERR_SUCCESS) {
            wm_log_error("lcd_clean_screen ret=%d", ret);
        }

        free(app_buf);
    }
}
WM_CLI_CMD_DEFINE(lcd, cmd_lcd, lcd cmd, lcd <on | off | image | red | green | blue | black | white | cyan | magenta | yellow> -- toggle screen or clear screen and display solid color);

static void cmd_beep(int argc, char *argv[])
{
    if (argc == 1) {
        wm_drv_gpio_data_set(KEY_PIN_BEEP);
        vTaskDelay(pdMS_TO_TICKS(130));
        wm_drv_gpio_data_reset(KEY_PIN_BEEP);
    } else if (argc == 2) {
        if (!strcmp(argv[1], "on")) {
            wm_drv_gpio_data_set(KEY_PIN_BEEP);
        } else if (!strcmp(argv[1], "off")) {
            wm_drv_gpio_data_reset(KEY_PIN_BEEP);
        }
    }
}
WM_CLI_CMD_DEFINE(beep, cmd_beep, beep cmd, beep [on | off] -- the buzzer sounds);

static void key_pin_isr_handler(void *arg)
{
    wm_gpio_num_t pin = (wm_gpio_num_t)arg;

    wm_log_info("GPIO[%d] intr, val: %d", (int)arg, wm_drv_gpio_data_get(pin));

    if (KEY_PIN_SW2 == pin)
        send_key_to_fastbee();
}

static void key_init(void)
{
    wm_drv_gpio_add_isr_callback(KEY_PIN_SW1, key_pin_isr_handler, (void *)KEY_PIN_SW1);
    wm_drv_gpio_add_isr_callback(KEY_PIN_SW2, key_pin_isr_handler, (void *)KEY_PIN_SW2);
    wm_drv_gpio_add_isr_callback(KEY_PIN_SW3, key_pin_isr_handler, (void *)KEY_PIN_SW3);
    wm_drv_gpio_add_isr_callback(KEY_PIN_SW4, key_pin_isr_handler, (void *)KEY_PIN_SW4);
    wm_drv_gpio_add_isr_callback(KEY_PIN_SW5, key_pin_isr_handler, (void *)KEY_PIN_SW5);
}

static void eth_init(void)
{
    int ret = WM_ERR_SUCCESS;
    wm_netif_t *netif;

    ret = wm_netif_init(NULL);

    if (!ret) {
        ret = emac_opencores_init(512, 5);
    }

    if (!ret) {
        ret = wm_netif_addif(WM_NETIF_TYPE_ETH);
    }

    if (!ret) {
        emac_opencores_start();
        ret = emac_opencores_set_link(ETH_LINK_UP);
    }

    if (!ret) {
        netif = wm_netif_get_netif(WM_NETIF_TYPE_ETH);
        if (netif && netif->netif)
            netifapi_netif_set_link_down(netif->netif);
    }
}

int main(void)
{
    key_init();
    eth_init();

    return 0;
}
