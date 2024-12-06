#include <string.h>
#include "wmsdk_config.h"
#include "wm_drv_gpio.h"
#include "wm_dt.h"
#include "wm_cli.h"
#include "fastbee.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LED_PIN_RED                     WM_GPIO_NUM_40
#define LED_PIN_GREEN                   WM_GPIO_NUM_41
#define LED_PIN_BLUE                    WM_GPIO_NUM_42

static int switch_state = 0;

void led_on(void)
{
    wm_drv_gpio_data_set(LED_PIN_RED);
    wm_drv_gpio_data_set(LED_PIN_GREEN);
    wm_drv_gpio_data_set(LED_PIN_BLUE);
    switch_state = 1;
}

void led_off(void)
{
    wm_drv_gpio_data_reset(LED_PIN_RED);
    wm_drv_gpio_data_reset(LED_PIN_GREEN);
    wm_drv_gpio_data_reset(LED_PIN_BLUE);
    switch_state = 0;
}

int led_state(void)
{
    return switch_state;
}

void led_red(void)
{
    wm_drv_gpio_data_set(LED_PIN_RED);
    wm_drv_gpio_data_reset(LED_PIN_GREEN);
    wm_drv_gpio_data_reset(LED_PIN_BLUE);
    switch_state = 1;
}

void led_green(void)
{
    wm_drv_gpio_data_reset(LED_PIN_RED);
    wm_drv_gpio_data_set(LED_PIN_GREEN);
    wm_drv_gpio_data_reset(LED_PIN_BLUE);
    switch_state = 1;
}

void led_blue(void)
{
    wm_drv_gpio_data_reset(LED_PIN_RED);
    wm_drv_gpio_data_reset(LED_PIN_GREEN);
    wm_drv_gpio_data_set(LED_PIN_BLUE);
    switch_state = 1;
}

static void cmd_led(int argc, char *argv[])
{
    if (argc != 2)
        return;

    if (!strcmp("led_red", argv[0])) {
        if (!strcmp("on", argv[1])) {
            wm_drv_gpio_data_set(LED_PIN_RED);
        } else if (!strcmp("off", argv[1])) {
            wm_drv_gpio_data_reset(LED_PIN_RED);
        }
    } else if (!strcmp("led_green", argv[0])) {
        if (!strcmp("on", argv[1])) {
            wm_drv_gpio_data_set(LED_PIN_GREEN);
        } else if (!strcmp("off", argv[1])) {
            wm_drv_gpio_data_reset(LED_PIN_GREEN);
        }
    } else if (!strcmp("led_blue", argv[0])) {
        if (!strcmp("on", argv[1])) {
            wm_drv_gpio_data_set(LED_PIN_BLUE);
        } else if (!strcmp("off", argv[1])) {
            wm_drv_gpio_data_reset(LED_PIN_BLUE);
        }
    } else if (!strcmp("led", argv[0])) {
        if (!strcmp("on", argv[1])) {
            //wm_drv_gpio_data_set(LED_PIN_RED);
            //wm_drv_gpio_data_set(LED_PIN_GREEN);
            //wm_drv_gpio_data_set(LED_PIN_BLUE);
            led_on();
        } else if (!strcmp("off", argv[1])) {
            //wm_drv_gpio_data_reset(LED_PIN_RED);
            //wm_drv_gpio_data_reset(LED_PIN_GREEN);
            //wm_drv_gpio_data_reset(LED_PIN_BLUE);
            led_off();
        }
        led_state_upload(led_state());
    }
}
WM_CLI_CMD_DEFINE(led, cmd_led, led cmd, led <on | off> -- RGB three-color light switch); //cppcheck # [syntaxError]
WM_CLI_CMD_DEFINE(led_red, cmd_led, led cmd, led_red <on | off> -- RGB three-color light switch); //cppcheck # [syntaxError]
WM_CLI_CMD_DEFINE(led_green, cmd_led, led cmd, led_green <on | off> -- RGB three-color light switch); //cppcheck # [syntaxError]
WM_CLI_CMD_DEFINE(led_blue, cmd_led, led cmd, led_blue <on | off> -- RGB three-color light switch); //cppcheck # [syntaxError]

#ifdef __cplusplus
}
#endif
