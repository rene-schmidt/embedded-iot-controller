/* USER CODE BEGIN Header */
/******************************************************************************
 * File:    app_net.h
 * Brief:   Application network interface
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *****************************************************************************/
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef APP_NET_H
#define APP_NET_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <stdbool.h>

/* USER CODE BEGIN Includes */
/* USER CODE END Includes */

/* Exported constants --------------------------------------------------------*/
/* Your ports / defaults (must exist already in your project) */
#ifndef APP_UDP_PORT
#define APP_UDP_PORT        5005U
#endif

#ifndef APP_TCP_PORT
#define APP_TCP_PORT        6006U
#endif

#ifndef APP_RASPI_IP
#define APP_RASPI_IP        "192.168.1.50"
#endif

/* USER CODE BEGIN EC */
/* USER CODE END EC */

/* Exported types ------------------------------------------------------------*/
typedef struct
{
  uint32_t now_ms;
  int32_t  i2c_temp_c;
  char     can_0x101[64];
  char     can_0x120[64];
} AppTelemetry;

/* USER CODE BEGIN ET */
/* USER CODE END ET */

/* Exported functions prototypes ---------------------------------------------*/
/* init + poll */
void APP_NET_Init(void);
void APP_NET_Poll(uint32_t now_ms);

/* main-loop service (lwIP tick + periodic send) */
void APP_NET_Service(uint32_t now_ms);

/* send primitives */
bool APP_NET_SendUDP(const AppTelemetry *t);
bool APP_NET_SendTCP(const AppTelemetry *t);

/* TCP status */
bool APP_NET_TcpIsConnected(void);

/* remote config */
bool APP_NET_SetRemote(const char *ip_str,
                        uint16_t udp_port,
                        uint16_t tcp_port);

/* UI helpers: last payload snippets */
const char *APP_NET_GetLastUDP(void);
const char *APP_NET_GetLastTCP(void);

/* USER CODE BEGIN EFP */
/* USER CODE END EFP */

#ifdef __cplusplus
}
#endif

#endif /* APP_NET_H */
