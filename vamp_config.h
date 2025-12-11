/** @brief Configuración de VAMP
 * Esta sección contiene las definiciones y configuraciones necesarias
 * para el funcionamiento de VAMP.
 */

#ifndef _VAMP_CONFIG_H_
#define _VAMP_CONFIG_H_

#include <Arduino.h>
#include <IPAddress.h>

/** @brief Longitud máxima del payload VAMP (en bytes) */
#ifndef VAMP_MAX_PAYLOAD_SIZE
#define VAMP_MAX_PAYLOAD_SIZE 30
#endif // VAMP_MAX_PAYLOAD_SIZE

// Tamaño estándar del buffer de internet (request/response) - Optimizado para ESP8266
#ifndef VAMP_IFACE_BUFF_SIZE
#define VAMP_IFACE_BUFF_SIZE 2048  // Reducido de 4096 a 2048 bytes
#endif // VAMP_IFACE_BUFF_SIZE

/** @brief Longitud máxima del ID del gateway VAMP (en bytes) */
#ifndef VAMP_GW_NAME_MAX_LEN
#define VAMP_GW_NAME_MAX_LEN 16
#endif // VAMP_GW_NAME_MAX_LEN

/** @brief Tiempo de espera por una respuesta */
#define VAMP_ANSW_TIMEOUT 500

/** @brief   Máximo de fallos consecutivos antes de re-join */
#define MAX_SEND_FAILURES 3

/* Parámetros para la comunicación del gateway VAMP con el VREG */
/** @brief  ID del gateway VAMP */
//#ifndef VAMP_GW_ID
//#define VAMP_GW_ID  "GW_TEST_01"
//#endif // VAMP_GW_ID

/** @brief  URL del VAMP Registry */  
//#ifndef VAMP_VREG_RESOURCE
//#define VAMP_VREG_RESOURCE "http://10.1.111.249:8000/vreg/api/v1/gateway/sync/"
//#endif // VAMP_VREG_RESOURCE

/** @brief Comentar/descomentar para desablitar/habilitar debug */
#define VAMP_DEBUG

/** Estructura para configuración de red (IP estática o DHCP) */
typedef struct {
    String mode;      // "static" o "dhcp"
    IPAddress ip;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns1;
    IPAddress dns2;
} net_config_t;

/* Estructuras agrupadas para la configuración del gateway
   Mantener las propiedades antiguas para compatibilidad, pero
   preferir las sub-estructuras (VAMP, WIFI, NRF, STORAGE) en nuevo código.
*/
typedef struct {
    String gw_id;                // vamp_gw_id
    String gw_name;              // vamp_gw_name
    String vreg_resource;        // vamp_vreg_resource
    String gw_token;             // vamp_gw_token (opcional)
} vamp_settings_t;

typedef struct {
    String ssid;                 // wifi_ssid
    String password;             // wifi_password
    String ap_ssid;              // wifi_ap_ssid (opcional, ejemplo)
} wifi_settings_t;

typedef struct {
    int channel;                 // nrf_channel
    int retries;                 // nrf_retries
    int retry_delay;             // nrf_retry_delay
    int max_payload_size;        // nrf_max_payload_size
} nrf_config_t;

struct gw_config_t {
    /* Configuraciones de VAMP (compatibilidad antigua + nueva) */
    vamp_settings_t vamp;            // new grouped settings (preferred)

    /* Configuraciones de iface (WiFi) */
    wifi_settings_t wifi;            // new grouped wifi settings (preferred)

    /* Configuraciones de red */
    net_config_t net;                // ya existente: mode, ip, gateway, subnet, dns1, dns2

    /* Configuraciones de RF (NRF) */
    nrf_config_t nrf;                // new grouped nrf settings (preferred)

    /* Keep sd_enabled at top-level as requested */
    bool sd_enabled;                 // sd_enabled (top-level)
};

#endif //_VAMP_CONFIG_H_
