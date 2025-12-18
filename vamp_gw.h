/**
		* @file vamp_gw.h
		* @brief VAMP Gateway - Virtual Address Mapping Protocol Gateway
		* @version 1.0
		* @date 2025-07-15
		* @author Bernardo Yaser León Ávila
		* 
		* 
		* 
		* This file contains the definitions and declarations for the VAMP Gateway,
		* which acts as a bridge between VAMP devices and the internet.
		* The VAMP Gateway is responsible for managing the VAMP table,
		* handling communication with VREG, and facilitating data exchange
 */

#include "vamp_config.h"

#include <Arduino.h>

#ifndef _VAMP_GW_H_
#define _VAMP_GW_H_

/* Verificar disponibilidad de ArduinoJson */
#ifdef  __has_include
	#if __has_include(<ArduinoJson.h>)
		#define ARDUINOJSON_AVAILABLE
	#endif
#endif


/** @brief Headers HTTP para la comunicación con el VREG */
/* ToDo ¿¿?? */
#define VAMP_HTTP_VREG_HEADERS  "Accept: application/json\r\n" \
                                "X-VAMP-Gateway-ID: %s\r\n" \
                               /* "Connection: close\r\n" */


/* Máscaras y macros para el protocolo extendido [C][PP][LLLLL] */
#define VAMP_WSN_PROFILE_MASK     0x60              // Bits 6-5: Perfil (00, 01, 10, 11)
#define VAMP_WSN_LENGTH_MASK      0x1F              // Bits 4-0: Longitud (0-31)

#define VAMP_HTTP_METHOD_GET  	0
#define VAMP_HTTP_METHOD_POST 	1
#define VAMP_HTTP_METHOD_PUT 	2
#define VAMP_HTTP_METHOD_DELETE 3

/* Protocolos soportados  ??*/
#define VAMP_PROTOCOL_HTTP    0
#define VAMP_PROTOCOL_HTTPS   1
#define VAMP_PROTOCOL_MQTT    2
#define VAMP_PROTOCOL_COAP    3
#define VAMP_PROTOCOL_WEBSOCKET 4
#define VAMP_PROTOCOL_CUSTOM  15    // Para protocolos definidos por usuario



/* Type */
#define VAMP_DEV_TYPE_FIXED		'0'
#define VAMP_DEV_TYPE_DYNAMIC	'1'
#define VAMP_DEV_TYPE_AUTO		'2'
#define VAMP_DEV_TYPE_ORPHAN	'3'



// Configuración de tablas y direccionamiento - Optimizado para ESP8266
#define VAMP_PORT_BASE 8000          // Puerto base para NAT
#define VAMP_DEVICE_TIMEOUT 600000   // Timeout de dispositivo (10 minutos)


void vamp_table_init(void);


void vamp_table_sync(void);

/* ------------------- Gestion de mensajes WSN ------------------- */

/** 
 * @brief Verificar si algun dispositivo VAMP nos contactó
 *  Esta función se encarga de verificar si algún dispositivo VAMP nos ha contactado
 *  y, en caso afirmativo, procesa la solicitud.
 * 	@return Codigo de estado:
 * 				-3 si hubo error en el procesamiento de datos
 * 				-2 en caso  error de conexión con el chip (o de timeout????)
 *           	-1 datos de entrada inválidos o error en el procesamiento,
 *           	 0 de no recibir datos,
 * 				 1 si se procesaron datos correctamente
 * 				 2 si se procesaron comanados correctamente
 */
int8_t vamp_gw_wsn(void);

/* --------------------- Funciones públicas para web server -------------------- */


/** Inicializar el gateway VAMP con la configuración del servidor VREG
 * @param gw_config Puntero hacia la estructura donde esta toda la onfiguracion del gateway
*/
bool vamp_gw_vreg_init(const gw_config_t * gw_config);


/** Obtener el dispositivo VREG correspondiente al RF_ID
 * @param rf_id Identificador del dispositivo RF
 * @return Índice del dispositivo en la tabla VAMP o VAMP_MAX_DEVICES si no se encuentra
 */
uint8_t vamp_get_vreg_device(const uint8_t * rf_id);



#endif // _VAMP_GW_H_