/**
 * @file vamp.h
 * @brief VAMP - Virtual Address Mapping Protocol
 * @version 1.0
 * @date 2025-07-15
 * @author Bernardo Yaser León Ávila
 * * This file contains the definitions and declarations for the VAMP protocol used in the mote.
 * * The VAMP protocol is designed to facilitate communication between the mote and a IP network.
 * 
 * 
 * 
 * 
 * 
 * 
 * 
 */

#ifndef _VAMP_H_
#define _VAMP_H_

#include "../../../idos.h"


#ifdef NRF24L01
#define VAMP_MAX_PAYLOAD_SIZE 32
#endif // NRF24L01

#ifndef VAMP_MAX_PAYLOAD_SIZE
#define VAMP_MAX_PAYLOAD_SIZE 32
#endif // VAMP_MAX_PAYLOAD_SIZE

/* Dirección de broadcast para VAMP */
#define VAMP_BROADCAST_ADDR {0xFF, 0xFF, 0xFF, 0xFF, 0xFF} 

/** Message types (datos/comandos)
 * Se utiliza un solo byte para tanto identificar el tipo de mensaje como 
 * para el tamaño del mensaje teniendo en cuenta que el tamaño máximo del 
 * payload es de 32 bytes, entonce solo se necesita 6 bits para el tamaño
 * Solo tienen tamaño (0-32) los mensaje que contengan datos, por lo que
 * se utilizan el bit mas significativo del byte para identificar el tipo 
 * de mensaje (datos/comandos). 
 *  - Si es 0, es un mensaje de datos, y el resto de bits para el tamaño del 
 *    mensaje (0-32).
 *  - Si es 1, es un mensaje de comando, y el resto de bits se utilizan para
 *    identificar el comando en concreto. A partir de saber el comando, se
 *    puede saber el tratamiento para el resto del mensaje.
*/
#define VAMP_DATA       0x00
#define VAMP_JOIN_REQ   0x81
#define VAMP_JOIN_ACK   0x82

// Si se se quiere validar si un mensaje es de tipo VAMP
//bool is_vamp_message(const uint8_t *data, size_t length);

/** @brief Check if VAMP is joined
 * 
 * @return true if joined, false otherwise
 */
bool vamp_is_joined(void);

/** @brief Join Network
 * 
 */
bool vamp_join_network(void);

/** @brief Force rejoin to the network
 * 
 * This function resets the connection with the gateway and attempts to rejoin the network.
 * It is useful in cases where the connection is lost or needs to be re-established.
 * 
 * @return true if rejoin was successful, false otherwise
 */
bool vamp_force_rejoin(void);

/** @brief Send data using VAMP
 * 
 * @param data Pointer to the data to be sent
 * @param len Length of the data to be sent
 * @return bool true on success, false on failure
 */
bool vamp_send_data(const uint8_t * data, uint8_t len);

#endif // _VAMP_H_