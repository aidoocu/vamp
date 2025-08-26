/** @file vamp_table.h
 * 
 * 
 * 
 * 
 */

 #ifndef _VAMP_TABLE_H_
 #define _VAMP_TABLE_H_

#include <Arduino.h>

#include "vamp_kv.h"

/* Fecha de la última actualización de la tabla en UTC */
#define VAMP_TABLE_INIT_TSMP "2020-01-01T00:00:00Z"

/* Longitud de la dirección VAMP (en bytes) */
#ifndef VAMP_ADDR_LEN
#define VAMP_ADDR_LEN 5
#endif // VAMP_ADDR_LEN

/** @brief Dirección de broadcast VAMP */
#ifndef VAMP_BROADCAST_ADDR
#define VAMP_BROADCAST_ADDR 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
#endif // VAMP_BROADCAST_ADDR

#ifndef VAMP_NULL_ADDR
#define VAMP_NULL_ADDR 0x00, 0x00, 0x00, 0x00, 0x00
#endif // VAMP_NULL_ADDR

/** @brief Longitud máxima del endpoint VAMP (en bytes) */
#ifndef VAMP_ENDPOINT_MAX_LEN
#define VAMP_ENDPOINT_MAX_LEN 128
#endif // VAMP_ENDPOINT_MAX_LEN

/** @brief Longitud máxima del endpoint VAMP (en bytes) */
#ifndef VAMP_PROTOCOL_OPTIONS_MAX_LEN
#define VAMP_PROTOCOL_OPTIONS_MAX_LEN 512
#endif // VAMP_PROTOCOL_OPTIONS_MAX_LEN

/* Máximo 32 dispositivos (5 bits) */
#ifndef VAMP_MAX_DEVICES
#define VAMP_MAX_DEVICES 16
#endif // VAMP_MAX_DEVICES

/* Máximo 4 perfiles por dispositivo o dos bits */
#define VAMP_MAX_PROFILES 4

#define VAMP_MAX_UPDATE_BUFFER 2048

/* Status */
#define VAMP_DEV_STATUS_FREE		0x01	// Libre, un dispositivo así deberá ignorarse sus campos 
#define VAMP_DEV_STATUS_INACTIVE	0x02	// Inactivo y configurado
#define VAMP_DEV_STATUS_ACTIVE		0x03	// Activo y configurado
#define VAMP_DEV_STATUS_ADDED		0x04	// Recién agregado, y NO configurado
#define VAMP_DEV_STATUS_CACHE		0x05	// En caché y configurado
#define VAMP_DEV_STATUS_REQUEST		0x06	// Dispositivo que solicita unirse y aun no ha confirmado la unión

/** Perfil de comunicación VAMP
 * Este perfil se utiliza para definir la estructura de los mensajes que se reencaminan por
 * el gateway VAMP, desde los dispositivos y hasta el servidor final. Los dispositvos no pueden
 * gestionar ninguna de las estructuras ip-tcp-http... por lo que depende de este perfil.
 * 		@field protocol: 	Protocolo de comunicación (HTTP, MQTT, CoAP, etc.)
 * 		@field method: 		Método específico del protocolo (GET/POST para HTTP, PUB/SUB para MQTT, etc.)
 * 		@field endpoint_resource: URL/URI del endpoint (sin esquema de protocolo)
 * 		@field protocol_params:	Parámetros específicos del protocolo (headers HTTP, topics MQTT, options CoAP, etc.)
 * 		@field payload_template: Es la forma que se espera que tenga el payload, o sea, la forma
 * 						en que deberia estar organizado el mensaje que viene del dispositivo.
 */
typedef struct vamp_profile_t {
//	uint8_t protocol;				// Protocolo (HTTP, MQTT, CoAP, etc.)
	uint8_t method;					// Método específico del protocolo
	char * endpoint_resource;    	// URL/URI del endpoint sin esquema (dinámica)
	vamp_key_value_store_t protocol_options;	// Opciones específicas del protocolo (key-value)
	vamp_key_value_store_t query_params;		// Parámetros de consulta (key-value)
//	char * payload_template;		// Plantilla de payload
} vamp_profile_t;

/**                                     Tabla VAMP
 * La tabla VAMP se utiliza para almacenar información sobre los dispositivos en la red,
 * incluyendo su estado, dirección RF y otra información relevante. Esta tabla es
 * fundamental para el funcionamiento del gateway VAMP y su interacción con los
 * dispositivos.
 * Tiene la siguiente estructura:
 * 
 * !!!!!!!!!!!!!!!!! tabla mal!!!!!!!!!!!!!!
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
	uint8_t wsn_id;                                 // ID en la forma [VVV][IIIII]
	uint8_t status;                                 // Estado: 
	uint8_t type;                                   // Tipo: 0=fijo, 1=dínamico, 2=auto, 3=huérfano
	uint8_t rf_id[VAMP_ADDR_LEN];                   // RF_ID del dispositivo (5 bytes)
	uint32_t last_activity;                         // Timestamp de última actividad en millis()
	uint8_t profile_count;                         	// Número de perfiles configurados (1-4)
	vamp_profile_t profiles[VAMP_MAX_PROFILES];  	// Array de perfiles de comunicación
	char * data_buff;     							// Buffer para datos
	uint16_t ticket;                              	// Ticket de comunicación
	//uint32_t join_time;                          	// Timestamp de cuando se unió
} vamp_entry_t;


/** @brief Actualizar la tabla VAMP desde el VREG*/
void vamp_table_update(vamp_profile_t * vreg_profile);

/** @brief Verificar si la tabla ha sido inicializada
 *  @return true si la tabla ha sido inicializada, false de lo contrario
 */
bool vamp_is_table_initialized(void);

/** @brief Obtener timestamp de la última sincronización */
const char * vamp_get_last_sync_timestamp(void);

/** @brief Establecer el timestamp de la última sincronización */
void vamp_set_last_sync_timestamp(const char * timestamp);

/* --------------------- Manejo de nodos (entradas) -------------------- */

/** @brief Obtener la cantidad de dispositivos en la tabla 
 *  @return retorna la cantidad de dispositivos
 */
uint8_t vamp_get_device_count(void);

/** @brief Obtener entrada de la tabla por índice 
 *  @param index Índice de la entrada en la tabla
 *  @return Puntero a la entrada de la tabla o NULL si no existe
 */
vamp_entry_t * vamp_get_table_entry(uint8_t index);

/** @brief Limpiar una entrada de la tabla
 *  @param index Índice de la entrada en la tabla
 */
void vamp_clear_entry(int index);

/** @brief Agregar un dispositivo a la tabla
 *  @note Esta función agrega un dispositivo con el ID RF especificado a la 
 *  tabla VAMP, pero no lo inicializa ni define su perfil.
 *  @note Si no hay espacio, se reemplaza el dispositivo inactivo más antiguo
 *  @param rf_id ID del dispositivo
 *  @return Índice del dispositivo agregado o VAMP_MAX_DEVICES si falla
 */
uint8_t vamp_add_device(const uint8_t* rf_id);

/** @brief Buscar un dispositivo en la tabla
 *  @param rf_id ID del dispositivo
 *  @return The index of the device if found, VAMP_MAX_DEVICES not
 *  found otherwise */
uint8_t vamp_find_device(const uint8_t* rf_id);

/** @brief Remover dispositivo con "rf_id" de la tabla
 *  @param rf_id ID del dispositivo
 *  @return true si la operación fue exitosa, false de lo contrario
 */
bool vamp_remove_device(const uint8_t* rf_id);

/** @brief Detectar dispositivos expirados en la tabla
 */
void vamp_detect_expired(void);

/** @brief Busca el dispositivo inactivo más antiguo
 *  @return índice del dispositivo inactivo más antiguo o VAMP_MAX_DEVICES si no hay
 */
uint8_t vamp_get_oldest_inactive(void);



/* --------------------- Manejo de perfiles -------------------- */

/** @brief Obtener perfil específico de un dispositivo */
const vamp_profile_t* vamp_get_device_profile(uint8_t device_index, uint8_t profile_index);

/** @brief Configurar perfil específico de un dispositivo */
bool vamp_set_device_profile(uint8_t device_index, uint8_t profile_index, const vamp_profile_t* profile);

/** @brief Limpiar todos los perfiles de un dispositivo */
void vamp_clear_device_profiles(uint8_t device_index);

/** @brief Limpiar un perfil específico liberando memoria */
void vamp_clear_profile(vamp_profile_t* profile);


/* --------------------- Macros auxiliares --------------------- */

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

/** @brief Macro para verificar si una dirección es broadcast */
#define VAMP_IS_BROADCAST_ADDR(addr) ((addr)[0] == 0xFF && (addr)[1] == 0xFF && (addr)[2] == 0xFF && (addr)[3] == 0xFF && (addr)[4] == 0xFF)


/* --------------------- Funciones auxiliares --------------------- */

/** @brief Convertir una cadena hexadecimal a un ID RF
 *  @example "01A3F5C789" -> {0x01, 0xA3, 0xF5, 0xC7, 0x89}
 *  @param hex_str Cadena hexadecimal
 *  @param rf_id Puntero al ID RF resultante
 *  @return true si la conversión fue exitosa, false de lo contrario
 */
bool hex_to_rf_id(const char* hex_str, uint8_t* rf_id);

/** @brief Convertir un ID RF a una cadena hexadecimal
 *  @example {0x01, 0xA3, 0xF5, 0xC7, 0x89} -> "01A3F5C789"
 *  @param rf_id ID RF
 *  @param hex_str Cadena hexadecimal resultante
 */
void rf_id_to_hex(const uint8_t* rf_id, char* hex_str);

/** @brief Verificar si un ID RF es válido
 *  @param rf_id ID RF a verificar
 *  @return true si el ID RF es válido, false de lo contrario
 */
bool vamp_is_rf_id_valid(const uint8_t * rf_id);

/** @brief Generar un byte de ID a partir del índice de la tabla
 *  @param table_index Índice de la tabla
 *  @note El byte de ID se forma combinando un número de verificación
 *        (3 bits) y el índice de la tabla (5 bits) [VVV][IIIII]
 *  @example table_index=1, wsn_id=0b10100001 -> retorna 0b11000001
 *  @return Byte de ID generado
 */
uint8_t vamp_generate_id_byte(const uint8_t table_index);


#endif //_VAMP_TABLE_H_
