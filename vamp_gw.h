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
  uint8_t port;                                    // Puerto NAT calculado (8000 + verification * 32 + index)
  uint8_t status;                                   // Estado: 
  uint8_t type;                                     // Tipo: 0=fijo, 1=dínamico, 2=auto
  uint8_t rf_id[VAMP_ADDR_LEN];                     // RF_ID del dispositivo (5 bytes)
  char endpoint_resource[VAMP_ENDPOINT_MAX_LEN];    // URL del endpoint del dispositivo
  time_t last_activity;                           	// Timestamp de última actividad
  //uint32_t join_time;                             // Timestamp de cuando se unió
} vamp_entry_t;

/* Type */
#define VAMP_DEV_TYPE_FIXED		0x00
#define VAMP_DEV_TYPE_DYNAMIC	0x01
#define VAMP_DEV_TYPE_AUTO		0x02

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
#define VAMP_GW_SYNC_REQ		"gateway_sync_req"		// Sincronización con el VREG
#define VAMP_GW_SYNC_RESP 		"gateway_sync_resp"    	// Sincronización de la tabla VAMP con el VREG
#define VAMP_VREG_SYNC_REQ 		"vreg_sync_req"     	// Solicitud de sincronización de la tabla VAMP
#define VAMP_VREG_SYNC_RESP 	"vreg_sync_resp"    	// Respuesta de sincronización de la tabla VAMP

/** VREG Response types
 * Respuestas del VREG (Virtual REGistry).
 */
#define VAMP_VREG_SYNC_OK        "--data"              // Sincronización exitosa
#define VAMP_VREG_SYNC_UPDATED   "--updated"           // Tabla ya está actualizada
#define VAMP_VREG_SYNC_ERROR     "--error"             // Error en la sincronización




/* ---------------------- Funciones de tabla ---------------------- */

/** @brief Funciones que manejan la tabla unificada VAMP
 * 
 * @param index Table index 0 <= index < VAMP_MAX_DEVICES
 * @param rf_id Buffer to store the identifier (have VAMP_ADDR_LEN bytes)
 * @return true if the operation was successful, false otherwise
 */
bool vamp_add_device(const uint8_t* rf_id);
bool vamp_remove_device(const uint8_t* rf_id);
void vamp_clear_entry(int index);
/** @return The index of the device if found, VAMP_MAX_DEVICES not 
 *  found otherwise */
uint8_t vamp_find_device(const uint8_t* rf_id);

/** @brief Actualiza la tabla VAMP desde el servidor VREG */
void vamp_table_update(void);

/** @brief Buscar dispositivos expirados para marcarlos como inactivos. 
 * Esta función debe ser llamada periódicamente para asegurar que la 
 * tabla VAMP se mantenga actualizada. */
void vamp_detect_expired(void);

/** @brief Retornar el indice del dispositivo inactivo más antiguo o 
 * 	VAMP_MAX_DEVICES si no hay dispositivos inactivos */
uint8_t vamp_get_oldest_inactive(void);

/* --------------------- Funciones ID compacto -------------------- */
uint8_t vamp_generate_id_byte(const uint8_t index);


/* --------------------- Funciones auxiliares --------------------- */

bool hex_to_rf_id(const char* hex_str, uint8_t* rf_id);
bool vamp_process_sync_response(const char* csv_data);

/** 
 * @brief Initialize VAMP system with callback and server configuration
 * 
 * @param vamp_internet_callback Callback function for HTTP communication
 * @param vamp_wsn_callback Callback function for WSN communication
 * @param vamp_vreg VREG server resource
 * @param vamp_gw_id Gateway ID string
 */
void vamp_local_gw_init(vamp_internet_callback_t vamp_internet_callback, vamp_wsn_callback_t vamp_wsn_callback, const char * vamp_vreg, const char * vamp_gw_id);


#endif // _VAMP_GW_H_