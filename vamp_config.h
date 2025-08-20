/** @brief Configuración de VAMP
 * Esta sección contiene las definiciones y configuraciones necesarias
 * para el funcionamiento de VAMP.
 */

#ifndef _VAMP_CONFIG_H_
#define _VAMP_CONFIG_H_

/* Longitud de la dirección VAMP (en bytes) */
#ifndef VAMP_ADDR_LEN
#define VAMP_ADDR_LEN 5
#endif // VAMP_ADDR_LEN

/** @brief Longitud máxima del payload VAMP (en bytes) */
#ifndef VAMP_MAX_PAYLOAD_SIZE
#define VAMP_MAX_PAYLOAD_SIZE 30
#endif // VAMP_MAX_PAYLOAD_SIZE

/** @brief Longitud máxima del endpoint VAMP (en bytes) */
#ifndef VAMP_ENDPOINT_MAX_LEN
#define VAMP_ENDPOINT_MAX_LEN 128
#endif // VAMP_ENDPOINT_MAX_LEN

/** @brief Longitud máxima del endpoint VAMP (en bytes) */
#ifndef VAMP_PROTOCOL_PARAMS_MAX_LEN
#define VAMP_PROTOCOL_PARAMS_MAX_LEN 512
#endif // VAMP_PROTOCOL_PARAMS_MAX_LEN

// Tamaño estándar del buffer de internet (request/response)
#ifndef VAMP_IFACE_BUFF_SIZE
#define VAMP_IFACE_BUFF_SIZE 4096
#endif // VAMP_IFACE_BUFF_SIZE

/** @brief Longitud máxima del ID del gateway VAMP (en bytes) */
#ifndef VAMP_GW_ID_MAX_LEN
#define VAMP_GW_ID_MAX_LEN 16
#endif // VAMP_GW_ID_MAX_LEN

/** @brief Dirección de broadcast VAMP */
#define VAMP_BROADCAST_ADDR {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}
#define VAMP_NULL_ADDR {0x00, 0x00, 0x00, 0x00, 0x00}

/** @brief Tiempo de espera por una respuesta */
#define VAMP_ANSW_TIMEOUT 1000

/** @brief   Máximo de fallos consecutivos antes de re-join */
#define MAX_SEND_FAILURES 3

/* Parámetros para la comunicación del gateway VAMP con el VREG */
/** @brief  ID del gateway VAMP */
#ifndef VAMP_GW_ID
#define VAMP_GW_ID  "GW_TEST_01"
#endif

/** @brief  URL del VAMP Registry */
#ifndef VAMP_VREG_RESOURCE
#define VAMP_VREG_RESOURCE	"http://10.1.111.249:8000/vreg/api/v1/gateway/sync/" // URL del VAMP Registry
#endif

/** @brief Comentar/descomentar para desablitar/habilitar debug */
#define VAMP_DEBUG


#endif //_VAMP_CONFIG_H_
