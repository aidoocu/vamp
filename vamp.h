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
 */

#ifndef _VAMP_H_
#define _VAMP_H_

#include <Arduino.h>

#include "vamp_config.h"

#define VAMP_RMODE_A 0b00000001 // Modo low power A
#define VAMP_RMODE_B 0b00000010 // Modo always listen B

/* Perfil por defecto */
#define VAMP_DEFAULT_PROFILE 0

/* Macros para extraer campos del protocolo extendido */
#define VAMP_WSN_GET_PROFILE(byte)    (((byte) & VAMP_WSN_PROFILE_MASK) >> 5)
#define VAMP_WSN_GET_LENGTH(byte)     ((byte) & VAMP_WSN_LENGTH_MASK)

/* Macros para crear el byte de protocolo extendido */
#define VAMP_WSN_MAKE_DATA_BYTE(profile, length)    (((profile) << 5) | ((length) & 0x1F))
#define VAMP_WSN_MAKE_CMD_BYTE(profile, cmd)        (VAMP_IS_CMD_MASK | ((profile) << 5) | ((cmd) & 0x1F))

/* Valores especiales para longitud */
#define VAMP_WSN_LENGTH_ESCAPE        31            // Longitud = 31 indica escape (datos adicionales siguen)


/** Client Message types (datos/comandos)
 * Se utiliza un solo byte para tanto identificar el tipo de mensaje como 
 * para el tamaño del mensaje teniendo en cuenta que el tamaño máximo del 
 * payload es de 32 bytes, entonce solo se necesita 6 bits para el tamaño
 * Solo tienen tamaño (0-32) los mensaje que contengan datos, por lo que
 * se utilizan el bit mas significativo del byte para identificar el tipo 
 * de mensaje (datos/comandos). 
 *  - Si es 0, es un mensaje de datos, y el resto de bits para el tamaño del 
 *    mensaje (0-32). @note que no necesita mascara.
 *  - Si es 1, es un mensaje de comando, y el resto de bits se utilizan para
 *    identificar el comando en concreto. A partir de saber el comando, se
 *    puede saber el tratamiento para el resto del mensaje.
*/
#define VAMP_IS_CMD_MASK    0x80
#define VAMP_WSN_CMD_MASK   0x7F

#define VAMP_JOIN_REQ       0x01
#define VAMP_JOIN_OK        0x02
#define VAMP_PING           0x03
#define VAMP_PONG           0x04
#define VAMP_POLL           0x05
#define VAMP_TICKET         0x06

/*  Largo que deberian tener cada uno de los mensajes de comando para poder 
    verificarlos */
#define VAMP_JOIN_REQ_LEN   0x06 // 1 byte comando + 5 bytes RF_ID


/** Métodos VAMP
 *
 * Estos métodos se utilizan para unificar la comunicación entre el gateway,
 * los dispositivos, el registro VAMP y los recursos de los dispositivos.
 * Se utilizan solo para servir como una traducción de los métodos de transporte
 * que se utilizan en la comunicación:
 *  - VAMP_ASK: Método de petición, utilizado para solicitar información o recursos.
 *              Equivalente a HTTP GET, MQTT SUBSCRIBE o radio READ.
 *  - VAMP_TELL: Método de respuesta, utilizado para enviar información o recursos.
 *              Equivalente a HTTP POST, MQTT PUBLISH o radio WRITE.
 */
//#define VAMP_ASK    0x00
//#define VAMP_TELL   0x01


/**
 * @brief Initialize VAMP system with callback and server configuration
 * 
 * @param vamp_vreg VREG server resource
 * @param wsn_id local WSN ID (5 bytes)
 */
void vamp_gw_init(const gw_config_t * gw_config, uint8_t * wsn_id);

/**
 * @brief Synchronize VAMP gateway with VREG server
 */
void vamp_gw_sync(void);


/** @brief Check if the RF_ID is valid
 * 
 * This function checks if the given RF_ID is valid.
 * A valid RF_ID should not be NULL, should not be a broadcast address (0xFF...),
 * and should not be a null address (0x00...).
 * 
 * @param rf_id Pointer to the RF_ID to check
 * @return true if the RF_ID is valid, false otherwise
 */
bool vamp_is_rf_id_valid(const uint8_t * rf_id);

#endif // _VAMP_H_