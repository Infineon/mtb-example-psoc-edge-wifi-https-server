/*******************************************************************************
* File Name: secure_http_server.h
*
* Description: This file contains configuration parameters for the secure HTTP
* server along with the HTML page that the server will host.
*
* Related Document: See README.md
********************************************************************************
* (c) 2024-2026, Infineon Technologies AG, or an affiliate of Infineon
* Technologies AG. All rights reserved.
* This software, associated documentation and materials ("Software") is
* owned by Infineon Technologies AG or one of its affiliates ("Infineon")
* and is protected by and subject to worldwide patent protection, worldwide
* copyright laws, and international treaty provisions. Therefore, you may use
* this Software only as provided in the license agreement accompanying the
* software package from which you obtained this Software. If no license
* agreement applies, then any use, reproduction, modification, translation, or
* compilation of this Software is prohibited without the express written
* permission of Infineon.
*
* Disclaimer: UNLESS OTHERWISE EXPRESSLY AGREED WITH INFINEON, THIS SOFTWARE
* IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, ALL WARRANTIES OF NON-INFRINGEMENT OF
* THIRD-PARTY RIGHTS AND IMPLIED WARRANTIES SUCH AS WARRANTIES OF FITNESS FOR A
* SPECIFIC USE/PURPOSE OR MERCHANTABILITY.
* Infineon reserves the right to make changes to the Software without notice.
* You are responsible for properly designing, programming, and testing the
* functionality and safety of your intended application of the Software, as
* well as complying with any legal requirements related to its use. Infineon
* does not guarantee that the Software will be free from intrusion, data theft
* or loss, or other breaches ("Security Breaches"), and Infineon shall have
* no liability arising out of any Security Breaches. Unless otherwise
* explicitly approved by Infineon, the Software may not be used in any
* application where a failure of the Product or any consequences of the use
* thereof can reasonably be expected to result in personal injury.
*******************************************************************************/

/*******************************************************************************
* Include guard
*******************************************************************************/
#ifndef SECURE_HTTP_SERVER_H_
#define SECURE_HTTP_SERVER_H_

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>
#include "cy_wcm.h"
#include "cybsp.h"
#include "cy_network_mw_core.h"

/* Wi-Fi Credentials: Modify WIFI_SSID and WIFI_PASSWORD to match your
 * Wi-Fi network Credentials.
 */
#define WIFI_SSID                 "MY_WIFI_SSID"
#define WIFI_PASSWORD             "MY_WIFI_PASSWORD"

/* Security type of the Wi-Fi access point. See 'cy_wcm_security_t' structure
 * in "cy_wcm.h" for more details.
 */
#define WIFI_SECURITY_TYPE        CY_WCM_SECURITY_WPA2_AES_PSK

#define MAX_WIFI_RETRY_COUNT      (3U)

#define CHECK_RESULT(x)           do { if (CY_RSLT_SUCCESS != x)  \
                                                    { CY_ASSERT(0); } } while(0);
#define APP_INFO(x)               do { printf("Info: "); printf x; } while(0);
#define ERR_INFO(x)               do { printf("Error: "); printf x; } while(0);

#define ERR_INFO_MDNS(eval, msg, ret)            if ((eval))           \
                                                 {                     \
                                                      ERR_INFO((msg)); \
                                                      return (ret);    \
                                                 }

#define PRINT_AND_ASSERT(result, msg, args...)   \
                                 do                                 \
                                 {                                  \
                                     if (CY_RSLT_SUCCESS != result) \
                                     {                              \
                                         ERR_INFO((msg, ## args));  \
                                         CY_ASSERT(0);              \
                                     }                              \
                                  } while(0);

#define HTTPS_SERVER_NAME                        "mysecurehttpserver"
#define MDNS_TTL_SECONDS                         (255U)
#define LED_STATUS_ON                            "ON"
#define LED_STATUS_OFF                           "OFF"
#define MAX_LED_STATUS_LENGTH                    (3U)
#define HTTPS_PORT                               (50007U)
#define MAX_SOCKETS                              (2U)
#define HTTPS_GET_RESPONSE                       "Hello - Message from HTTPS "\
                                                 "server!"
#define REGISTER_RESOURCE_TASK_STACK_SIZE        (1024U)
#define REGISTER_RESOURCE_TASK_PRIORITY          (tskIDLE_PRIORITY)
#define REGISTER_RESOURCE_QUEUE_LENGTH           (1U)
#define NEW_RESOURCE_NAME_LENGTH                 (30U)
#define HTTPS_RESOURCE_PUT_ERROR                 "Maximum resource name length" \
                                                 "is %d characters."
#define HTTPS_REQUEST_HANDLE_SUCCESS             (0U)
#define HTTPS_REQUEST_HANDLE_ERROR               (-1)

#define HTTPS_STARTUP_WEBPAGE \
              "<!DOCTYPE html>" \
              "<html>" \
              "<head>" \
              "<title>HTTPS Server Demo</title>" \
              "</head>" \
              "<body>" \
                "<h1>HTTPS Server Demo</h1>" \
                "<form method=\"get\">" \
                    "<fieldset>" \
                        "<legend>HTTPS GET</legend>" \
                        "<input type=\"submit\" value=\"Get LED status\"/>" \
                        "<input type=\"text\" name=\"led_status\" value=\"%s\" " \
                        "size=\"3\" readonly/></br></br>" \
                    "</fieldset>" \
                    "</br>" \
                "</form>" \
                "<form method=\"post\">" \
                    "<fieldset>" \
                        "<legend>HTTPS POST</legend>" \
                        "<input type=\"submit\" name=\"toggle_led\" value=\"Toggle " \
                        "LED\"/></br></br>" \
                    "</fieldset>" \
                    "</br>" \
                "</form>" \
              "</body>" \
              "</html>"

#define MAX_HTTP_RESPONSE_LENGTH  (sizeof(HTTPS_STARTUP_WEBPAGE) + \
                                   sizeof(MAX_LED_STATUS_LENGTH) + 1)

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
void https_server_task(void *arg);
#endif /* SECURE_HTTP_SERVER_H_ */
