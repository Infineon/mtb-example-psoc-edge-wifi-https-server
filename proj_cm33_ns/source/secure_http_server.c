/*******************************************************************************
* File Name: secure_http_server.c
*
* Description: This file contains the task that connects to the Wi-Fi Access
* Point, starts the mDNS (multicast DNS) and then starts the HTTP Server
* in secure mode. This file also contains the necessary functions to process 
* GET, POST, and PUT requests from the HTTPS client.
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

/* Header file includes */
#include "cybsp.h"
#include "retarget_io_init.h"

/* FreeRTOS header file */
#include <FreeRTOS.h>
#include <task.h>

/* Secure Sockets header file */
#include "cy_secure_sockets.h"
#include "cy_tls.h"

/* Wi-Fi connection manager header files */
#include "cy_wcm.h"
#include "cy_wcm_error.h"

/* Standard C header file */
#include <string.h>
#include <stdio.h>

/* HTTPS server task header file. */
#include "secure_http_server.h"
#include "cy_http_server.h"
#include "secure_keys.h"

/* MDNS responder header file */
#include "mdns.h"

/*******************************************************************************
* Macros
*******************************************************************************/
#define APP_SDIO_INTERRUPT_PRIORITY                  (7U)
#define APP_HOST_WAKE_INTERRUPT_PRIORITY             (2U)
#define APP_SDIO_FREQUENCY_HZ                        (25000000U)
#define SDHC_SDIO_64BYTES_BLOCK                      (64U)
#define INITIAL_VALUE                                (0U)
#define TICKS_TO_WAIT                                (0U)
#define STRING_CMP_SUCCESS                           (0U)
#define OFFSET_VALUE                                 (1U)
#define MEMSET_VALUE                                 (0U)
#define IFACE_IDX                                    (0U)

/*******************************************************************************
* Global Variables
*******************************************************************************/
/* Holds the IP address obtained using Wi-Fi Connection Manager (WCM). */
static cy_wcm_ip_address_t ip_addr;

/* Secure HTTP server instance. */
static cy_http_server_t https_server;

/* Wi-Fi network interface. */
static cy_network_interface_t nw_interface;

/* Holds the IP address and port number details of the socket for the HTTPS
 * server.
 */
static cy_socket_sockaddr_t https_ip_address;

/* Holds the security configuration such as server certificate, server key,
 * and root_ca.
 */
static cy_https_server_security_info_t security_config;

/* Holds the response handler for HTTPS GET and POST request from the client. */
static cy_resource_dynamic_data_t https_get_post_resource;

/* Holds the user data which adds/updates the URL data resources. */
static cy_resource_dynamic_data_t https_put_resource;

/* Queues the HTTPS PUT request to register new page resource in the server. */
static QueueHandle_t register_resource_queue_handle;

/* Holds the current LED status. */
static char *led_status = LED_STATUS_OFF;

/* HTTPS new page resource name */
static char resource_name[NEW_RESOURCE_NAME_LENGTH] = {INITIAL_VALUE};

/* Global variable to track number of resources registered. */
static uint32_t number_of_resources_registered = INITIAL_VALUE;

/* Holds all the URL resources along with its data. */
typedef struct
{
    char *resource_name;
    char *value;
    uint32_t length;
} https_url_database_t;

/* Initializes the URL database with first entry reserved for HTTPS root URL.
 * The default value of MAX_NUMBER_OF_HTTP_SERVER_RESOURCES is 10 as defined
 * by the HTTPS server middleware. This can be overriden by setting its value
 * in the application Makefile. Refer to README.md for details.
 */
static https_url_database_t url_resources_db[MAX_NUMBER_OF_HTTP_SERVER_RESOURCES] =
{
    /* First entry in the URL database is reserved for HTTPS Root URL. */
    { (char *)"/", NULL, INITIAL_VALUE }
};

/* SDIO and SDHC instance */
static mtb_hal_sdio_t sdio_instance;
static cy_stc_sd_host_context_t sdhc_host_context;
static cy_wcm_config_t wcm_config;

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

/* SysPm callback parameter structure for SDHC */
static cy_stc_syspm_callback_params_t sdcardDSParams =
{
        .context   = &sdhc_host_context,
        .base      = CYBSP_WIFI_SDIO_HW
};

/* SysPm callback structure for SDHC */
static cy_stc_syspm_callback_t sdhcDeepSleepCallbackHandler =
{
    .callback           = Cy_SD_Host_DeepSleepCallback,
    .skipMode           = SYSPM_SKIP_MODE,
    .type               = CY_SYSPM_DEEPSLEEP,
    .callbackParams     = &sdcardDSParams,
    .prevItm            = NULL,
    .nextItm            = NULL,
    .order              = SYSPM_CALLBACK_ORDER
};

#endif

/*******************************************************************************
* Function Prototypes
*******************************************************************************/
static cy_rslt_t configure_https_server(void);

/*******************************************************************************
* Function Name: dynamic_resource_handler
********************************************************************************
* Summary:
*  Handles HTTPS GET, POST, and PUT requests from the client.
*  HTTPS GET sends the current CYBSP_USER_LED status as a response to the client.
*  HTTPS POST toggles the CYBSP_USER_LED and then sends its current state as a
*  response to the client.
*  HTTPS PUT sends an error message as a response to the client if the resource
*  registration is unsuccessful.
*
* Parameters:
*  url_path - Pointer to the HTTPS URL path.
*  url_parameters - Pointer to the HTTPS URL query string.
*  stream - Pointer to the HTTPS response stream.
*  arg - Pointer to the argument passed during HTTPS resource registration.
*  https_message_body - Pointer to the HTTPS data from the client.
*
* Return:
*  int32_t - Returns HTTPS_REQUEST_HANDLE_SUCCESS if the request from the client
*  was handled successfully. Otherwise, it returns HTTPS_REQUEST_HANDLE_ERROR.
*
*******************************************************************************/
static int32_t dynamic_resource_handler(const char* url_path,
                                 const char* url_parameters,
                                 cy_http_response_stream_t* stream,
                                 void* arg,
                                 cy_http_message_body_t* https_message_body)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int32_t status = HTTPS_REQUEST_HANDLE_SUCCESS;
    char https_response[MAX_HTTP_RESPONSE_LENGTH] = {INITIAL_VALUE};
    char *register_new_resource = NULL;

    switch (https_message_body->request_type)
    {
        case CY_HTTP_REQUEST_GET:

            APP_INFO(("Received HTTPS GET request.\n"));

           /* Update the current LED status. This status will be sent
            * in response to the POST request.
            */
            if (CYBSP_LED_STATE_ON == Cy_GPIO_Read(CYBSP_USER_LED_PORT,
                    CYBSP_USER_LED_PIN))
            {
                led_status = LED_STATUS_ON;
            }
            else
            {
                led_status = LED_STATUS_OFF;
            }

            sprintf(https_response, HTTPS_STARTUP_WEBPAGE, led_status);

            /* Send the HTTPS response. */
            result = cy_http_server_response_stream_write_payload(stream,
                    https_response, sizeof(https_response));

            if (CY_RSLT_SUCCESS != result)
            {
                ERR_INFO(("Failed to send the HTTPS GET response.\n"));
            }

            break;

        case CY_HTTP_REQUEST_POST:

            APP_INFO(("Received HTTPS POST request.\n"));

           /* Send the current LED status before toggling in response to the
            * POST request. The user can then send a GET request to get the
            * latest LED status on the webpage.
            */
            sprintf(https_response, HTTPS_STARTUP_WEBPAGE, led_status);

            /* Toggle the user LED. */
            Cy_GPIO_Inv(CYBSP_USER_LED_PORT, CYBSP_USER_LED_PIN);

            /* Send the HTTPS response. */
            result = cy_http_server_response_stream_write_payload(stream,
                    https_response, sizeof(https_response));

            if (CY_RSLT_SUCCESS != result)
            {
                ERR_INFO(("Failed to send the HTTPS POST response.\n"));
            }

            break;

        case CY_HTTP_REQUEST_PUT:

            if (https_message_body->data_length > sizeof(resource_name))
            {
                /* Report the error response to the client. */
                ERR_INFO(("Resource name length exceeded the limit. Maximum: %d, "
                        "Received: %d", sizeof(resource_name),
                        (https_message_body->data_length)));
                sprintf(https_response, HTTPS_RESOURCE_PUT_ERROR,
                        sizeof(resource_name));

                /* Send the HTTPS error response. */
                result = cy_http_server_response_stream_write_payload(stream,
                        https_response, sizeof(https_response));

                if (CY_RSLT_SUCCESS != result)
                {
                    ERR_INFO(("Failed to send the HTTPS PUT error response.\n"));
                }
            }
            else
            {
                register_new_resource = (char *)&resource_name[0];
                memcpy(register_new_resource, (char *)https_message_body->data,
                        https_message_body->data_length);

               /* Received HTTPS PUT request. Put the message into queue
                * to register a new resource with the HTTPS server.
                */
                if (pdTRUE != xQueueSend(register_resource_queue_handle,
                        (void *)&register_new_resource, TICKS_TO_WAIT))
                {
                    ERR_INFO(("Failed to send queue message.\n"));
                }
            }

            break;

        default:

            ERR_INFO(("Received invalid HTTP request method. Supported "
                      "HTTP methods are GET, POST, and PUT.\n"));
            break;
    }

    return status;
}

/*******************************************************************************
* Function Name: https_put_resource_handler
********************************************************************************
* Summary:
*  Handles HTTPS GET request for the newly created resource by the client
*  through HTTPS PUT request. It returns the value registered with the requested
*  resource.
*
* Parameters:
*  url_path - Pointer to the HTTPS URL path.
*  url_parameters - Pointer to the HTTPS URL query string.
*  stream - Pointer to the HTTPS response stream.
*  arg - Pointer to the argument passed during HTTPS resource registration.
*  https_message_body - Pointer to the HTTPS data from the client.
*
* Return:
*  int32_t - Returns HTTPS_REQUEST_HANDLE_SUCCESS if the request from the client
*  was handled successfully. Otherwise, it returns HTTPS_REQUEST_HANDLE_ERROR.
*
*******************************************************************************/
static int32_t https_put_resource_handler(const char *url_path,
                                   const char *url_parameters,
                                   cy_http_response_stream_t *stream,
                                   void *arg,
                                   cy_http_message_body_t *https_message_body)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    int32_t status = HTTPS_REQUEST_HANDLE_SUCCESS;
    uint32_t index = INITIAL_VALUE;

    if (CY_HTTP_REQUEST_GET == https_message_body->request_type)
    {
        APP_INFO(("Received HTTPS GET request.\n"));

        while (MAX_NUMBER_OF_HTTP_SERVER_RESOURCES > index)
        {
            if (STRING_CMP_SUCCESS == strcmp(url_resources_db[index].
                    resource_name, (char *)arg))
            {
                result = cy_http_server_response_stream_write_payload(stream,
                                               url_resources_db[index].value,
                                             url_resources_db[index].length);
                break;
            }

            /* Get next entry. */
            index++;
        }
    }

    if (CY_RSLT_SUCCESS != result)
    {
        ERR_INFO(("Failed to send the response message.\n"));
        status = HTTPS_REQUEST_HANDLE_ERROR;
    }

    return status;
}

/*******************************************************************************
* Function Name: register_https_resource
********************************************************************************
* Summary:
*  Registers/Updates the new resource with the HTTPS server when HTTPS PUT
*  request is received from the client.
*
* Parameters:
*  register_resource_name: Pointer to the resource name to be registered
*   with the HTTPS server.
*
* Return:
*  None
*
*******************************************************************************/
static void register_https_resource(char *register_resource_name)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    char *url_resource_data = NULL;
    uint32_t index = INITIAL_VALUE;
    APP_INFO(("New resource to create: %s\n", register_resource_name));

    /* Configure dynamic resource handler. */
    https_put_resource.resource_handler = https_put_resource_handler;
    https_put_resource.arg = NULL;

    /* Split the URL resource name and data from the HTTPS PUT request. */
    url_resource_data = strtok(register_resource_name, "=");
    url_resource_data = strtok(NULL, "=");
    APP_INFO(("New URL: %s, Response text: %s\n", register_resource_name,
            url_resource_data));

   /* Register the resource if it does not exist in the URL database or
    * update the resource data if the requested resource already
    * exists in the URL database.
    */
    while (MAX_NUMBER_OF_HTTP_SERVER_RESOURCES > index)
    {

        if ((NULL != url_resources_db[index].resource_name) &&
             STRING_CMP_SUCCESS == strcmp(url_resources_db[index].resource_name,
             register_resource_name))
        {
            APP_INFO(("Updating the existing resource: %s\n\n",
                    register_resource_name));
            url_resources_db[index].length = strlen(url_resource_data);
            url_resources_db[index].value =
                    (char *)realloc(url_resources_db[index].value,
                    (url_resources_db[index].length + OFFSET_VALUE));
            memset(url_resources_db[index].value, MEMSET_VALUE,
                    (url_resources_db[index].length + OFFSET_VALUE));
            memcpy(url_resources_db[index].value, url_resource_data,
                    url_resources_db[index].length);
            https_put_resource.arg = url_resources_db[index].resource_name;
            break;
        }
        else if (NULL == url_resources_db[index].resource_name)
        {
            APP_INFO(("Registering the new resource: %s\n\n",
                    register_resource_name));
            url_resources_db[index].value =
                    (char *)calloc((strlen(url_resource_data) + OFFSET_VALUE),
                            sizeof(char));
            url_resources_db[index].resource_name =
                    (char *)calloc((strlen(register_resource_name) +
                     OFFSET_VALUE), sizeof(char));

            if ((NULL == url_resources_db[index].resource_name) ||
                (NULL == url_resources_db[index].value))
            {
                 ERR_INFO(("Failed to allocate memory for URL resources.\n"));
                 handle_app_error();
            }

            /* Add the resource name, data, and data length into URL database. */
            url_resources_db[index].length = strlen(url_resource_data);
            memcpy(url_resources_db[index].resource_name, register_resource_name,
                                                 strlen(register_resource_name));
            memcpy(url_resources_db[index].value, url_resource_data,
                                                 url_resources_db[index].length);
            https_put_resource.arg = url_resources_db[index].resource_name;

            printf("Registering the resource\n");
            /* Register the new/updated resource with HTTPS server. */
            result = cy_http_server_register_resource(https_server,
                      (uint8_t*)url_resources_db[index].resource_name,
                      (uint8_t*)"text/html", CY_DYNAMIC_URL_CONTENT,
                       &https_put_resource);
            PRINT_AND_ASSERT(result, "Failed to register a new resource.\n");

            /* Update the resource count. */
            number_of_resources_registered++;
            break;
        }
        else
        {
            /* Get next resource. */
            index++;
        }
    }

    if (MAX_NUMBER_OF_HTTP_SERVER_RESOURCES <= index)
    {
        ERR_INFO(("Requested resource not registered/updated. Reached Maximum "
                  "allowed number of resource registration: %d\n",
                  MAX_NUMBER_OF_HTTP_SERVER_RESOURCES));
    }

    /* Clear the request buffer. */
    memset(resource_name, MEMSET_VALUE, sizeof(resource_name));
}

/*******************************************************************************
* Function Name: configure_https_server
********************************************************************************
* Summary:
*  Configures the security parameters such as server certificate, private key,
*  and the root CA certificate to start the HTTP server in secure mode. By
*  default, it registers a dynamic URL handler to handle the HTTPS GET, POST,
*  and PUT requests.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t: Returns CY_RSLT_SUCCESS if the secure HTTP server is configured
*  successfully, otherwise, it returns CY_RSLT_TYPE_ERROR.
*
*******************************************************************************/
static cy_rslt_t configure_https_server(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

   /* Setup the necessary security configuration required for establishing
    * secure HTTP connection with the secure HTTP client. This example uses
    * a self-signed certificate and is not trusted by the browser. Browsers
    * trust only the certificates signed by a CA (certificate authority).
    * To make the browser trust the connection with this HTTP server, user
    * needs to load the server certificate into the browser.
    */
    security_config.certificate          = (uint8_t *)keySERVER_CERTIFICATE_PEM;
    security_config.certificate_length   = strlen(keySERVER_CERTIFICATE_PEM);
    security_config.private_key          = (uint8_t *)keySERVER_PRIVATE_KEY_PEM;
    security_config.key_length           = strlen(keySERVER_PRIVATE_KEY_PEM);
    security_config.root_ca_certificate  = (uint8_t *)keyCLIENT_ROOTCA_PEM;
    security_config.root_ca_certificate_length = strlen(keyCLIENT_ROOTCA_PEM);

    /* IP address of server. */
    https_ip_address.ip_address.ip.v4 = ip_addr.ip.v4;
    https_ip_address.ip_address.version = CY_SOCKET_IP_VER_V4;

    /* Add IP address information to network interface object. */
    nw_interface.object  = (void *) &https_ip_address;
    nw_interface.type = CY_NW_INF_TYPE_WIFI;

    /* Initialize secure socket library. */
    result = cy_http_server_network_init(); 
    PRINT_AND_ASSERT(result, "Failed to intialize the secure sockets "
            "library.\n");

    /* Allocate memory needed for secure HTTP server. */
    result = cy_http_server_create(&nw_interface, HTTPS_PORT, MAX_SOCKETS,
            &security_config, &https_server);
    PRINT_AND_ASSERT(result, "Failed to allocate memory for the HTTPS server.\n");

    /* Configure dynamic resource handler. */
    https_get_post_resource.resource_handler = dynamic_resource_handler;
    https_get_post_resource.arg = NULL;

    /* Register all the resources with the secure HTTP server. */
    result = cy_http_server_register_resource(https_server,
                                              (uint8_t*)"/",
                                              (uint8_t*)"text/html",
                                              CY_DYNAMIC_URL_CONTENT,
                                              &https_get_post_resource);
    PRINT_AND_ASSERT(result, "Failed to register the resources with the secure "
                             "HTTP server.\n");

    /* Update the resource count. */
    number_of_resources_registered++;

    return result;
}

/*******************************************************************************
* Function Name: mdns_responder_start
********************************************************************************
* Summary:
* Starts the mDNS responder using lwIP network stack APIs. It resolves the IP
* address for a given hostname and sends the DNS response to the DNS client.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t: Returns CY_RSLT_SUCCESS if the MDNS has started successfully.
*
*******************************************************************************/
static cy_rslt_t mdns_responder_start(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;

   /* Start lwIP's MDNS responder.
    * MDNS is required to resolve the local domain name of the secure HTTP server.
    */
    struct netif *net = cy_network_get_nw_interface
            (CY_NETWORK_WIFI_STA_INTERFACE, IFACE_IDX);
    mdns_resp_init();

    if (ERR_OK != mdns_resp_add_netif(net, HTTPS_SERVER_NAME, MDNS_TTL_SECONDS))
    {
        ERR_INFO(("Failed to start the MDNS responder.\n"));
        result = CY_RSLT_TYPE_ERROR;
    }

    return result;
}
/*******************************************************************************
* Function Name: sdio_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for SDIO instance.
*******************************************************************************/
static void sdio_interrupt_handler(void)
{
    mtb_hal_sdio_process_interrupt(&sdio_instance);
}

/*******************************************************************************
* Function Name: host_wake_interrupt_handler
********************************************************************************
* Summary:
* Interrupt handler function for the host wake up input pin.
*******************************************************************************/
static void host_wake_interrupt_handler(void)
{
    mtb_hal_gpio_process_interrupt(&wcm_config.wifi_host_wake_pin);
}

/*******************************************************************************
* Function Name: app_sdio_init
********************************************************************************
* Summary:
* This function configures and initializes the SDIO instance used in
* communication between the host MCU and the wireless device.
*******************************************************************************/
static void app_sdio_init(void)
{
    cy_rslt_t result;
    mtb_hal_sdio_cfg_t sdio_hal_cfg;

    cy_stc_sysint_t sdio_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_SDIO_IRQ,
        .intrPriority = APP_SDIO_INTERRUPT_PRIORITY
    };

    cy_stc_sysint_t host_wake_intr_cfg =
    {
        .intrSrc = CYBSP_WIFI_HOST_WAKE_IRQ,
        .intrPriority = APP_HOST_WAKE_INTERRUPT_PRIORITY
    };

    /* Initialize the SDIO interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status = Cy_SysInt_Init(&sdio_intr_cfg,
            sdio_interrupt_handler);

    /* SDIO interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_SDIO_IRQ);

    /* Setup SDIO using the HAL object and desired configuration */
    result = mtb_hal_sdio_setup(&sdio_instance, &CYBSP_WIFI_SDIO_sdio_hal_config,
            NULL, &sdhc_host_context);

    /* SDIO setup failed. Stop program execution. */
    if(CY_RSLT_SUCCESS != result)
    {
        handle_app_error();
    }

    /* Initialize and Enable SD HOST */
    Cy_SD_Host_Enable(CYBSP_WIFI_SDIO_HW);
    Cy_SD_Host_Init(CYBSP_WIFI_SDIO_HW,
            CYBSP_WIFI_SDIO_sdio_hal_config.host_config, &sdhc_host_context);
    Cy_SD_Host_SetHostBusWidth(CYBSP_WIFI_SDIO_HW, CY_SD_HOST_BUS_WIDTH_4_BIT);
    sdio_hal_cfg.frequencyhal_hz = APP_SDIO_FREQUENCY_HZ;
    sdio_hal_cfg.block_size = SDHC_SDIO_64BYTES_BLOCK;

    /* Configure SDIO */
    mtb_hal_sdio_configure(&sdio_instance, &sdio_hal_cfg);

#if (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP)

    /* SDHC SysPm callback registration */
    Cy_SysPm_RegisterCallback(&sdhcDeepSleepCallbackHandler);

#endif /* (CY_CFG_PWR_SYS_IDLE_MODE == CY_CFG_PWR_MODE_DEEPSLEEP) */

    /* Setup GPIO using the HAL object for WIFI WL REG ON  */
    mtb_hal_gpio_setup(&wcm_config.wifi_wl_pin, CYBSP_WIFI_WL_REG_ON_PORT_NUM,
            CYBSP_WIFI_WL_REG_ON_PIN);

    /* Setup GPIO using the HAL object for WIFI HOST WAKE PIN  */
    mtb_hal_gpio_setup(&wcm_config.wifi_host_wake_pin,
            CYBSP_WIFI_HOST_WAKE_PORT_NUM, CYBSP_WIFI_HOST_WAKE_PIN);

    /* Initialize the Host wakeup interrupt and specify the interrupt handler. */
    cy_en_sysint_status_t interrupt_init_status_host_wake =
            Cy_SysInt_Init(&host_wake_intr_cfg, host_wake_interrupt_handler);

    /* Host wake up interrupt initialization failed. Stop program execution. */
    if(CY_SYSINT_SUCCESS != interrupt_init_status_host_wake)
    {
        handle_app_error();
    }

    /* Enable NVIC interrupt. */
    NVIC_EnableIRQ(CYBSP_WIFI_HOST_WAKE_IRQ);
}

/*******************************************************************************
* Function Name: wifi_connect
********************************************************************************
* Summary:
*  The device associates to the Access Point with given SSID, PASSWORD, and
*  SECURITY type. It retries for MAX_WIFI_RETRY_COUNT times if the Wi-Fi
*  connection fails.
*
* Parameters:
*  void
*
* Return:
*  cy_rslt_t: Returns CY_RSLT_SUCCESS if the Wi-Fi connection is successfully
*  established, a WCM error code otherwise.
*
*******************************************************************************/
static cy_rslt_t wifi_connect(void)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    uint32_t retry_count = INITIAL_VALUE;
    cy_wcm_connect_params_t connect_param;
    memset(&connect_param, INITIAL_VALUE, sizeof(connect_param));

    /* Initialize SDIO instance */
    app_sdio_init();

    /* Initialize the Wi-Fi device as a STA.*/
    wcm_config.interface = CY_WCM_INTERFACE_TYPE_STA;
    wcm_config.wifi_interface_instance = &sdio_instance;
    result = cy_wcm_init(&wcm_config);

    if (CY_RSLT_SUCCESS == result)
    {
        APP_INFO(("Wi-Fi initialization is successful\n"));
        memcpy(&connect_param.ap_credentials.SSID, WIFI_SSID, sizeof(WIFI_SSID));
        memcpy(&connect_param.ap_credentials.password, WIFI_PASSWORD,
                sizeof(WIFI_PASSWORD));
        connect_param.ap_credentials.security = WIFI_SECURITY_TYPE;
        APP_INFO(("Join to AP: %s\n", connect_param.ap_credentials.SSID));

       /* Connect to Access Point. It validates the connection parameters
        * and then establishes connection to AP.
        */
        for (retry_count = INITIAL_VALUE; retry_count < MAX_WIFI_RETRY_COUNT;
                retry_count++)
        {
            result = cy_wcm_connect_ap(&connect_param, &ip_addr);

            if (CY_RSLT_SUCCESS == result)
            {
                APP_INFO(("Successfully joined Wi-Fi network %s\n",
                        connect_param.ap_credentials.SSID));

                if (CY_WCM_IP_VER_V4 == ip_addr.version)
                {
                    APP_INFO(("Assigned IP address: %s\n",
                            ip4addr_ntoa((const ip4_addr_t *)&ip_addr.ip.v4)));
                }
                else if (CY_WCM_IP_VER_V6 == ip_addr.version)
                {
                    APP_INFO(("Assigned IP address: %s\n",
                            ip6addr_ntoa((const ip6_addr_t *)&ip_addr.ip.v6)));
                }

                break;
            }

            ERR_INFO(("Failed to join Wi-Fi network. Retrying...\n"));
        }
    }
    else
    {
        handle_app_error();
    }

    return result;
}

/*******************************************************************************
* Function Name: https_server_task
*******************************************************************************
* Summary:
*  Starts the HTTP server in secure mode. This example application is using a
*  self-signed certificate which means there is no third-party certificate issuing
*  authority involved in the authentication of the server. It is the user's
*  responsibility to supply the necessary security configurations such as server's
*  certificate, private key of the server, and RootCA of the server to start the
*  HTTP server in secure mode.
*
* Parameters:
*  arg - Unused.
*
* Return:
*  None.
*
*******************************************************************************/
void https_server_task(void *arg)
{
    cy_rslt_t result = CY_RSLT_SUCCESS;
    char *register_new_resource_name = NULL;
    CY_UNUSED_PARAMETER(arg);

    /* Connects to the Wi-Fi Access Point. */
    result = wifi_connect();
    PRINT_AND_ASSERT(result, "Wi-Fi connection failed.\n");

    /* Resolves the HTTPS server name to an IP address. */
    result = mdns_responder_start();
    PRINT_AND_ASSERT(result, "Failed to start MDNS responder.\n");

   /* Configure the HTTPS server with all the security parameters and
    * register a default dynamic URL handler.
    */
    result = configure_https_server();
    PRINT_AND_ASSERT(result, "Failed to configure the HTTPS server.\n");

    /* Start the HTTPS server. */
    result = cy_http_server_start(https_server);
    PRINT_AND_ASSERT(result, "Failed to start the HTTPS server.\n");

   /* This queue is used to register a new HTTPS page resource when the HTTPS
    * server receives a PUT request sent by the client.
    */
    register_resource_queue_handle = xQueueCreate(REGISTER_RESOURCE_QUEUE_LENGTH,
                                                          sizeof(resource_name));

    if (NULL == register_resource_queue_handle)
    {
        ERR_INFO(("Failed to create the queue.\n"));
        handle_app_error();
    }

    APP_INFO(("HTTPS server has successfully started. The server is running at "
              "URL https://%s.local:%d\n\n", HTTPS_SERVER_NAME, HTTPS_PORT));

   /* Waits for a HTTPS PUT request from the client to register a new HTTPS
    * page resource.
    */
    while(true)
    {
        if (pdTRUE == xQueueReceive(register_resource_queue_handle,
                                    &register_new_resource_name,
                                    portMAX_DELAY))
        {
            APP_INFO(("New resource name register request: %s\n",
                    register_new_resource_name));
            register_https_resource(register_new_resource_name);
        }
    }
}


/* [] END OF FILE */
