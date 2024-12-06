#ifndef __FASTBEE_H__
#define __FASTBEE_H__

#ifdef __cplusplus
extern "C" {
#endif

void send_key_to_fastbee(void);

void led_on(void);
void led_off(void);

int led_state(void);

void led_state_upload(int state);

void led_red(void);
void led_green(void);
void led_blue(void);
int set_firmware_type(char *fw_type);

int ota_parse_update(char *payload);

#ifdef __cplusplus
}
#endif

#endif /* __FASTBEE_H__ */
