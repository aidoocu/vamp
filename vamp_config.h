/** @brief Configuración de VAMP
 * Esta sección contiene las definiciones y configuraciones necesarias
 * para el funcionamiento de VAMP.
 */

#ifndef _VAMP_CONFIG_H_
#define _VAMP_CONFIG_H_

#include <Arduino.h>
#include <IPAddress.h>

/** @brief Comentar/descomentar si el hardware tiene una SD asociada
 *  @note Toda la configuracion de la SD esta hecha en la app asi que el 
 * codigo asociado a la SD dentro de VAMP simplemente intentara utilizarla
 * y si falla no hara nada al respecto.
 */
#define VAMP_SD

/** @brief Directorio donde se salvan las lineas de datos */
#define DATA_DIR "/DATA_MOTE"

/** @brief Longitud máxima del payload VAMP (en bytes) */
#ifndef VAMP_MAX_PAYLOAD_SIZE
#define VAMP_MAX_PAYLOAD_SIZE 30
#endif // VAMP_MAX_PAYLOAD_SIZE

// Tamaño estándar del buffer de internet (request/response) - Optimizado para ESP8266
#ifndef VAMP_IFACE_BUFF_SIZE
#define VAMP_IFACE_BUFF_SIZE 2048  // Reducido de 4096 a 2048 bytes
#endif // VAMP_IFACE_BUFF_SIZE

/** @brief Longitud máxima del ID del gateway VAMP (en bytes) */
#ifndef VAMP_GW_ID_MAX_LEN
#define VAMP_GW_ID_MAX_LEN 11
#endif // VAMP_GW_ID_MAX_LEN

#ifndef VAMP_GW_NAME_MAX_LEN
#define VAMP_GW_NAME_MAX_LEN 32
#endif // VAMP_GW_NAME_MAX_LEN

#ifndef VAMP_SSID_MAX_LEN
#define VAMP_SSID_MAX_LEN 32
#endif // VAMP_SSID_MAX_LEN

#ifndef VAMP_PASSWORD_MAX_LEN
#define VAMP_PASSWORD_MAX_LEN 64
#endif // VAMP_PASSWORD_MAX_LEN

/** @brief Tiempo de espera por una respuesta */
#define VAMP_ANSW_TIMEOUT 500

/** @brief   Máximo de fallos consecutivos antes de re-join */
#define MAX_SEND_FAILURES 3

/** @brief Modos de configuracion para IP */
#define VAMP_NET_DHCP       0
#define VAMP_NET_STATIC     1

/** @brief Comentar/descomentar para desablitar/habilitar debug */
//#define VAMP_DEBUG

/** Estructura para configuración de red (IP estática o DHCP) */
typedef struct {
    uint8_t     mode;      // VAMP_NET_DHCP o VAMP_NET_STATIC
    IPAddress   ip;
    IPAddress   gateway;
    IPAddress   subnet;
    IPAddress   dns1;
    IPAddress   dns2;
} net_config_t;

/* Estructuras agrupadas para la configuración del gateway
   Mantener las propiedades antiguas para compatibilidad, pero
   preferir las sub-estructuras (VAMP, WIFI, NRF, STORAGE) en nuevo código.
*/
typedef struct {
    char * gw_id;                // vamp_gw_id
    char * gw_name;              // vamp_gw_name
    char * vreg_resource;        // vamp_vreg_resource
    char * gw_token;             // vamp_gw_token (opcional)
} vamp_settings_t;

typedef struct {
    char * ssid;                 // wifi_ssid
    char * password;             // wifi_password
    //char * ap_ssid;              // wifi_ap_ssid (opcional, ejemplo)
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
