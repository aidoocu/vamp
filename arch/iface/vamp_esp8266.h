/** 
 * 
 * 
 * 
 */
#ifndef VAMP_ESP8266_IFACE_H_
#define VAMP_ESP8266_IFACE_H_

#include <Arduino.h>
#include "../../vamp_config.h"
#include "../../lib/vamp_table.h"


#define TLS_BUFFER_SIZE_TX 3072
#define TLS_BUFFER_SIZE_RX 2048

/** Mínimo de heap para TLS
 * El BearSSL la primera vez que intenta una conexion usa 6KB de stack, los reserva
 * en el heap y no los libera más en toda la vida del programa. Asi que cuando se
 * inicializa la interfaz WiFi se hace una conexión TLS de prueba para reservar ese heap.
 * Si no hay suficiente heap libre, la conexión TLS falla y no se puede usar HTTPS.
 * El valor de MIN_HEAP_FOR_TLS define el mínimo de heap libre que debe haber para
 * alojar los buffers de Tx y Rx, más los aproximadamente 2KB de overhead de BearSSL, 
 * no incluyen los 6KB de stack inicial que ya se reservan en la primera conexión.
 * El valor recomendado es 16KB para tener un margen de seguridad, pero se ajustará
 * según los recursos del sistema.
 * @note Este valor es crítico para el funcionamiento de HTTPS en ESP8266
 * @note Si se usan certificados, este valor debe ser mayor.
 * @note Este valor no puede ser menor que los buffers que van a procesar los datos.
*/
#define MIN_HEAP_FOR_TLS 2000 + TLS_BUFFER_SIZE_TX + TLS_BUFFER_SIZE_RX /* 2KB overhead + Tx + Rx buffers */

 /** @brief Inicializa la WiFi station (STA) en el ESP8266
  * 
  * @param vamp_conf Configuración del gateway
  * @return true si la conexión fue exitosa, false en caso contrario
  */
bool esp8266_sta_init(const gw_config_t * vamp_conf);

/** @brief Realiza una solicitud HTTP/HTTPS
 * 
 * @param profile Perfil de comunicación
 * @param data Datos a enviar
 * @param data_size Tamaño del buffer data
 * @return Tamaño de los datos recibidos, 0 en caso de error
 */
size_t esp8266_http_request(const vamp_profile_t * profile, char * data, size_t data_size);

#endif // VAMP_ESP8266_IFACE_H_