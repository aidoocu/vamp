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


#ifndef _VAMP_GW_H_
#define _VAMP_GW_H_


#include "vamp.h"


/* Fecha de la última actualización de la tabla en UTC */
#define VAMP_TABLE_INIT_TSMP "2020-01-01T00:00:00Z"

#define VAMP_HTTP_METHOD_GET  0
#define VAMP_HTTP_METHOD_POST 1


/** Perfil de comunicación VAMP
 * Este perfil se utiliza para definir la estructura de los mensajes que se reencaminan por
 * el gateway VAMP, desde los dispositivos y hasta el servidor final. Los dispositvos no pueden
 * gestionar ninguna de las estructuras ip-tcp-http... por lo que depende de este perfil.
 * 		@field endpoint_resource: URL del endpoint del dispositivo
 * 		@field method: 	Método HTTP (GET, POST,...) con el que se enviara el mensaje. 
 * 						@todo hay que pensar en MQTT ++
 * 		@field headers:	Cabeceras HTTP ( @todo hay que pensar en MQTT ++ )
 * 		@field payload_template: Es la forma que se espera que tenga el payload, o sea, la forma
 * 						en que deberia estar organizado el mensaje que viene del dispositivo.
 */
typedef struct {
	uint8_t method;					// Método (GET, POST,...)
	char * endpoint_resource;    	// URL del endpoint del dispositivo (dinámica)
	char * headers;					// Cabeceras HTTP
//	char * payload_template;		// Plantilla de payload
} vamp_profile_t;

/**                                     Tabla VAMP
 * La tabla VAMP se utiliza para almacenar información sobre los dispositivos en la red,
 * incluyendo su estado, dirección RF y otra información relevante. Esta tabla es
 * fundamental para el funcionamiento del gateway VAMP y su interacción con los
 * dispositivos.
 * Tiene la siguiente estructura:
 * | Puerto |  Estado  |  Tipo    |  Dirección RF  |  Resource          | last_activity (milis) |
 * |--------|----------|----------|----------------|--------------------|-----------------------|
 * | 8128   | Libre    | Fijo     | 01:23:45:67:89 | dev1.org/local     |		107360  		|
 * | 8161   | Activo   | Dinámico | AA:BB:CC:DD:EE | my.io/sense/temp   | 		201565	 		|
 * | 8226   | Inactivo | Auto     | 10:20:30:40:50 | tiny.net/hot       | 		300000  		|
 * | 8035   | Libre    | Fijo     | DE:AD:BE:EF:00 | hot.dog/ups        | 		400507		 	|
 *
 * Los puerto se forman para buscar compatibilidad (aparente) con NAT a partir de un prefijo
 * (el 8) + número de verificación de 3 bits (0-7) y un índice de dispositivo de 5 bits (0-31).
 * Por ejemplo, el puerto 8161 al corresponde 8 + 161 (0b10100001) donde el número de verificación
 * es 5 (0b101) y un índice de dispositivo de 1 (0b00001).
 * Se utiliza un prefijo de 8 para evitar conflictos con puertos reservados y asegurar que
 * los puertos generados no se superpongan con otros servicios.
 * El indice de dispositivo se utiliza para localizar el dispositivo en la tabla VAMP sin tener que
 * buscar.
 * El numero de verificación se utiliza para poder reutilizar los indices. Interfaces como la
 * nRF24 no exponen su dirección RF al receptor y enviarla en el payload es demasiado costoso.
 * En este caso con el número de verificación (3 bits) + índice (5 bit) se puede identificar 
 * el dispositivo con solo un byte de ID compacto.
 */
typedef struct {
	uint8_t wsn_id;                                   // ID en la forma [VVV][IIIII]
	uint8_t status;                                   // Estado: 
	uint8_t type;                                     // Tipo: 0=fijo, 1=dínamico, 2=auto, 3=huérfano
	uint8_t rf_id[VAMP_ADDR_LEN];                     // RF_ID del dispositivo (5 bytes)
	uint32_t last_activity;                           // Timestamp de última actividad en millis()
	vamp_profile_t profile;                           // Perfil de comunicación con el dispositivo
	//uint32_t join_time;                             // Timestamp de cuando se unió
} vamp_entry_t;


/* Type */
#define VAMP_DEV_TYPE_FIXED		'0'
#define VAMP_DEV_TYPE_DYNAMIC	'1'
#define VAMP_DEV_TYPE_AUTO		'2'
#define VAMP_DEV_TYPE_ORPHAN	'3'

/* Status */
#define VAMP_DEV_STATUS_FREE		0x01
#define VAMP_DEV_STATUS_INACTIVE	0x02
#define VAMP_DEV_STATUS_ACTIVE		0x03
#define VAMP_DEV_STATUS_ADDED		0x04
#define VAMP_DEV_STATUS_CACHE		0x05

// Configuración de tablas y direccionamiento
#define VAMP_MAX_DEVICES 32          // Máximo número de dispositivos (5 bits)
#define VAMP_PORT_BASE 8000          // Puerto base para NAT
#define VAMP_DEVICE_TIMEOUT 600000   // Timeout de dispositivo (10 minutos)

/**
 * Macros para manejo de ID compacto (verification + index en 1 byte)
 * 
 * El ID compacto permite:
 * - Acceso directo a tabla: tabla[VAMP_GET_INDEX(id_byte)]
 * - Verificación de consistencia: VAMP_GET_VERIFICATION(id_byte) 
 * - Generación automática de puerto: VAMP_MAKE_PORT(verification, index)
 * 
 * Formato del byte ID: [VVV][IIIII] donde V=verificación (3 bits), I=índice (5 bits)
 */
#define VAMP_MAKE_ID_BYTE(verification, index) (((verification & 0x07) << 5) | (index & 0x1F))
#define VAMP_GET_INDEX(id_byte) (id_byte & 0x1F)
#define VAMP_GET_VERIFICATION(id_byte) ((id_byte >> 5) & 0x07)
//#define VAMP_MAKE_PORT(verification, index) (VAMP_PORT_BASE + (verification << 5) + index)



/** VREG Command types
* Estos comandos se utilizan para la comunicación con el VREG (Virtual REGistry).
*/
#define VAMP_GW_SYNC	 "sync"				// Sincronización con el VREG
#define VAMP_GET_NODE	 "get_node"		// Obtener nodo VREG
#define VAMP_SET_NODE	 "set_node"		// Establecer nodo VREG

/* Opciones */
#define VAMP_GATEWAY_ID	"--gateway"        // ID del gateway
#define VAMP_TIMESTAMP	"--timestamp"      // Última actualización de la tabla VAMP
#define VAMP_NODE_ID		"--node_rf_id"     // ID del nodo VREG
#define VAMP_DEV_COUNT	"--device_count"   // Cantidad de dispositivos en la tabla VAMP

/** VREG Response types
 * Respuestas del VREG (Virtual REGistry).
 */
#define VAMP_DATA   		"--data"			// Sincronización exitosa
#define VAMP_UPDATED		"--updated"		// Tabla ya está actualizada
#define VAMP_ERROR			"--error"			// Error en la sincronización




/* ---------------------- Funciones de tabla ---------------------- */

/** @brief Funciones que manejan la tabla unificada VAMP
 * 
 * @param index Table index 0 <= index < VAMP_MAX_DEVICES
 * @param rf_id Buffer to store the identifier (have VAMP_ADDR_LEN bytes)
 * @return true if the operation was successful, false otherwise
 */
bool vamp_remove_device(const uint8_t* rf_id);
void vamp_clear_entry(int index);
/** @return The index of the device if found, VAMP_MAX_DEVICES not 
 *  found otherwise */
uint8_t vamp_find_device(const uint8_t* rf_id);
uint8_t vamp_add_device(const uint8_t* rf_id);
uint8_t vamp_get_vreg_device(const uint8_t * rf_id);

/** @brief Actualiza la tabla VAMP desde el servidor VREG */
void vamp_table_update(void);

/** @brief Buscar dispositivos expirados para marcarlos como inactivos. 
 * Esta función debe ser llamada periódicamente para asegurar que la 
 * tabla VAMP se mantenga actualizada. */
void vamp_detect_expired(void);

/** @brief Retornar el indice del dispositivo inactivo más antiguo o 
 * 	VAMP_MAX_DEVICES si no hay dispositivos inactivos */
uint8_t vamp_get_oldest_inactive(void);

/* --------------------- Funciones tabla VAMP -------------------- */

/** @brief Obtener número de dispositivos activos en la tabla */
uint8_t vamp_get_device_count(void);

/** @brief Obtener entrada de la tabla por índice
 * @param index Índice del dispositivo (0-31)
 * @return Puntero a la entrada o NULL si índice inválido */
const vamp_entry_t* vamp_get_table_entry(uint8_t index);

/** @brief Obtener estado legible de un dispositivo */
const char* vamp_get_status_string(uint8_t status);

/** @brief Obtener tipo legible de un dispositivo */
const char* vamp_get_type_string(uint8_t type);

/* --------------------- Funciones ID compacto -------------------- */
uint8_t vamp_generate_id_byte(const uint8_t index);


/* --------------------- Funciones auxiliares --------------------- */

bool hex_to_rf_id(const char* hex_str, uint8_t* rf_id);
void rf_id_to_hex(const uint8_t* rf_id, char* hex_str);
bool vamp_is_rf_id_valid(const uint8_t * rf_id);
bool vamp_process_sync_response(const char* csv_data);
bool vamp_get_timestamp(char * timestamp);


/* ------------------- Gestion de mensajes WSN ------------------- */

/* Verificar si es un comando o un dato */
#define VAMP_WSN_IS_COMMAND(buffer) ((buffer)[0] & VAMP_IS_CMD_MASK)
/* Aislar el comando */
#define VAMP_WSN_GET_CMD(buffer) ((buffer)[0] & VAMP_WSN_CMD_MASK)


/** 
 * @brief Verificar si algun dispositivo VAMP nos contactó
 *  Esta función se encarga de verificar si algún dispositivo VAMP nos ha contactado
 *  y, en caso afirmativo, procesa la solicitud.
 */
bool vamp_gw_wsn(void);

/* --------------------- Funciones públicas para web server -------------------- */

/** @brief Obtener timestamp de la última sincronización */
const char* vamp_get_last_sync_timestamp(void);

/** @brief Obtener número de dispositivos activos en la tabla */
uint8_t vamp_get_device_count(void);

/** @brief Obtener entrada de la tabla por índice */
const vamp_entry_t* vamp_get_table_entry(uint8_t index);

/** @brief Obtener estado legible de un dispositivo */
const char* vamp_get_status_string(uint8_t status);

/** @brief Obtener tipo legible de un dispositivo */
const char* vamp_get_type_string(uint8_t type);

/** @brief Verificar si la tabla ha sido inicializada */
bool vamp_is_table_initialized(void);


/** Inicializar el gateway VAMP con la configuración del servidor VREG
 * @param vreg_url Recurso VREG
 * @param gw_id ID del gateway
*/
void vamp_gw_vreg_init(char * vreg_url, char * gw_id);




#endif // _VAMP_GW_H_