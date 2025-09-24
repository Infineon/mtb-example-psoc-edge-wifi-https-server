[Click here](../README.md) to view the README.

## Design and implementation

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

<br>

In this code example, at device reset, the secure boot process starts from the ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. Resource initialization for this example is performed by this CM33 non-secure project. It configures the system clocks, pins, clock to peripheral connections, and other platform resources. It then enables the CM55 core using the `Cy_SysEnableCM55()` function and the CM55 core is subsequently put to DeepSleep mode.

In the CM33 non-secure application, the clocks and system resources are initialized by the BSP initialization function. The retarget-io middleware is configured to use the debug UART. The LPTimer is initialized to allow the system to enter into DeepSleep mode when the idle task is executed. A real-time clock (RTC) is initialized and a task named "HTTPS Server" is created and the RTOS scheduler starts.

In this example, the HTTPS Server establishes a secure connection with a web browser or cURL client through an SSL handshake. During the SSL handshake, the server presents its SSL certificate for verification and verifies the incoming client identity. This example uses the Multicast Domain Name System (mDNS) provided by the  Lightweight Internet Protocol (lwIP)/Transmission Control Protocol (TCP) open-source TCP/IP network stack. mDNS helps in resolving the domain name of the HTTPS Server to an IP address in the local network. This code example supports only Internet Protocol version 4 (IPv4) with mDNS.

You can define the maximum number of HTTPS page resources for the HTTPS Server in the application *Makefile* as shown below. The HTTPS Server library maintains the database of pages based on this value.

```
DEFINES+=MAX_NUMBER_OF_HTTP_SERVER_RESOURCES=10
```

Note that if the `MAX_NUMBER_OF_HTTP_SERVER_RESOURCES` value is not defined in the application *Makefile*, the HTTPS Server will set it to 10 by default. This code example does not define this parameter in the application *Makefile*. Therefore, the application uses the default value of 10.

<br>