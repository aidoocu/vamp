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

#include <Arduino.h>

#ifndef VAMP_ADDR_LEN
#define VAMP_ADDR_LEN 5
#endif // VAMP_ADDR_LEN

#ifndef VAMP_MAX_PAYLOAD_SIZE
#define VAMP_MAX_PAYLOAD_SIZE 32
#endif // VAMP_MAX_PAYLOAD_SIZE

#ifndef VAMP_ENDPOINT_MAX_LEN
#define VAMP_ENDPOINT_MAX_LEN 64
#endif // VAMP_ENDPOINT_MAX_LEN

#ifndef VAMP_GW_ID_MAX_LEN
#define VAMP_GW_ID_MAX_LEN 16
#endif // VAMP_GW_ID_MAX_LEN

/* Dirección de broadcast para VAMP */
#define VAMP_BROADCAST_ADDR {0xFF, 0xFF, 0xFF, 0xFF, 0xFF}

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
#define VAMP_JOIN_ACK       0x02
#define VAMP_PING           0x03
#define VAMP_PONG           0x04

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
#define VAMP_ASK    0
#define VAMP_TELL   1

/** @brief Callback function type for internet communication with VREG
 * 
 * The callback should send the data using the specified  method and return the response.
 * 
 * @param endpoint VREG endpoint resource (e.g., "vreg.example.com/sync")
 * @param method HTTP method: VAMP_ASK or VAMP_TELL
 * @param data Buffer containing request data or response data (modified by function)
 * @param data_size Size of data buffer
 * @return bool true on success, false on failure
 */
typedef bool (* vamp_internet_callback_t)(const char * endpoint, uint8_t method, char * data, size_t data_size);


/** @brief Callback function type for radio communication
 * 
 * Radio-specific callback that handles communication with devices over the radio.
 * The callback should send the data using the specified method and return true on success.
 * 
 * @param rf_id RF_ID of the device to communicate with
 * @param mode mode: VAMP_ASK or VAMP_TELL
 * @param data Buffer containing request data or response data
 * @param len Length of data buffer
 * @return If mode is VAMP_TELL, returns 1 on success, 0 on failure.
 *         If mode is VAMP_ASK, returns amount of data received, 0 means no data received.
 */
typedef uint8_t (* vamp_wsn_callback_t)(const char * rf_id, uint8_t mode, uint8_t * data, size_t len);

/**
 * @brief Initialize VAMP system with callback and server configuration
 * 
 * @param vamp_internet_callback Callback function for internet communication
 * @param vamp_wsn_callback Callback function for WSN communication
 * @param vamp_vreg VREG server resource
 * @param vamp_gw_id Gateway ID string
 */
void vamp_gw_init(vamp_internet_callback_t vamp_internet_callback, vamp_wsn_callback_t vamp_wsn_callback, const char * vamp_vreg_url, const char * vamp_gw_id);

/**
 * @brief Synchronize VAMP gateway with VREG server
 */
void vamp_gw_sync(void);

/** 
 * @brief Verificar si algun dispositivo VAMP nos contactó
 *  Esta función se encarga de verificar si algún dispositivo VAMP nos ha contactado
 *  y, en caso afirmativo, procesa la solicitud.
 */
void vamp_ask_wsn(void);

#endif // _VAMP_H_