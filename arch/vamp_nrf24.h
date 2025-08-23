/** 
 * 
 * 
 * 
 * 
 */

#ifndef ARCH_NRF24_H_
#define ARCH_NRF24_H_

#include <Arduino.h>

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
 * @param len Longitud de los datos
 * @param wsn_buff Puntero a los datos a enviar/recibir
 * @return Tamaño de los datos recibidos, 0 en caso de error
 */
uint8_t nrf_comm(uint8_t * dst_addr, uint8_t len, uint8_t * wsn_buff);

#endif /* ARCH_NRF24_H_ */
