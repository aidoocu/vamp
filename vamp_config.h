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

/** @brief Comentar/descomentar para desablitar/habilitar debug */
#define VAMP_DEBUG


#endif //_VAMP_CONFIG_H_
