#ifndef INC_ESP01S_H_
#define INC_ESP01S_H_
#include <stdbool.h>
#include <stdint.h>

void bsp_esp8266_Init(void);
bool bsp_esp8266_SendCommand(const char *cmd, const char *ack, uint32_t timeout);
bool esp_join_ap(const char* ssid, const char* pass, uint32_t timeout_ms);
#endif /* INC_ESP01S_H_ */