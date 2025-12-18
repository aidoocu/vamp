/** 
 * 
 * 
 * 
 * 
 */

#ifndef ARCH_NRF24_H_
#define ARCH_NRF24_H_

#include <Arduino.h>

#ifdef MOTE_IDOS_BOARD
	#define RF24_AVAILABLE
#endif

/** @brief Inicializa el módulo NRF24L01
 * 
 * @param ce_pin Pin de Chip Enable
 * @param csn_pin Pin de Chip Select Not
 * @param addr Dirección del nodo
 * @return true si la inicialización fue exitosa, false en caso contrario
 */
bool nrf_init(uint8_t ce_pin, uint8_t csn_pin, uint8_t * addr);


/** @brief Envía una solicitud al módulo NRF24L01 y espera una respuesta
 * 
 * @param dst_addr Dirección de destino
 * @param wsn_buff Puntero a los datos a enviar/recibir
 * @param len Longitud de los datos
 * @return  - Tamaño de los datos recibidos, 
 *          - 0 de no recibir datos,
 *          - -1 en caso de error en la solicitud, datos de entrada inválidos
 *			- -2 en caso  error de conexión con el chip (o de timeout????)
 * 
 */
int8_t nrf_comm(uint8_t * dst_addr, uint8_t * data, uint8_t len);

/** @brief Is chip active mode
 * 
 *  @return true si el chip está en modo activo, false en caso contrario
 */
bool nrf_is_chip_active(void);

/** @brief Obtiene la dirección local del WSN
 * 
 * @return Puntero a la dirección local del WSN
 */
uint8_t * nrf_get_local_wsn_addr(void);

/** @brief Establece la dirección local del WSN
 * 
 * @param addr Puntero a la nueva dirección local del WSN
 */
void nrf_set_local_wsn_addr(uint8_t * addr);

#endif /* ARCH_NRF24_H_ */
