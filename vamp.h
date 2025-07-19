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

// ======================== ESTRUCTURAS VAMP ========================

/* La tabla VAMP se utiliza para almacenar información sobre los dispositivos en la red,
 * incluyendo su estado, dirección RF y otra información relevante. Esta tabla es
 * fundamental para el funcionamiento del gateway VAMP y su interacción con los
 * dispositivos.
 * Tiene la siguiente estructura:
 * | Puerto |  Estado  |  Tipo    |  Dirección RF  |  Resource          | timestamp             |
 * |--------|----------|----------|----------------|--------------------|-----------------------|
 * | 8128   | Libre    | Fijo     | 01:23:45:67:89 | dev1.org/local     | 2025-07-18T00:00:00Z  |
 * | 8161   | Activo   | Dinámico | AA:BB:CC:DD:EE | my.io/sense/temp   | 2025-07-18T01:00:00Z  |
 * | 8226   | Inactivo | Auto     | 10:20:30:40:50 | tiny.net/hot       | 2025-07-18T02:00:00Z  |
 * | 8035   | Libre    | Fijo     | DE:AD:BE:EF:00 | hot.dog/ups        | 2025-07-18T03:00:00Z  |
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
  uint16_t port;                                    // Puerto NAT calculado (8000 + verification * 32 + index)
  uint8_t status;                                   // Estado: 0=libre, 1=activo, 2=timeout
  uint8_t type;                                     // Tipo: 0=fijo, 1=dínamico, 2=auto
  uint8_t rf_id[VAMP_ADDR_LEN];                     // RF_ID del dispositivo (5 bytes)
  char endpoint_resource[VAMP_ENDPOINT_MAX_LEN];    // URL del endpoint del dispositivo
  uint32_t last_activity;                           // Timestamp de última actividad
  //uint32_t join_time;                             // Timestamp de cuando se unió
} vamp_entry_t;

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
#define VAMP_MAKE_PORT(verification, index) (VAMP_PORT_BASE + (verification << 5) + index)

// Estados para vamp_entry_t.status
#define VAMP_STATUS_FREE 0           // Entrada libre
#define VAMP_STATUS_ACTIVE 1         // Dispositivo activo
#define VAMP_STATUS_TIMEOUT 2        // Dispositivo en timeout 

/** Client Message types (datos/comandos)
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
#define VAMP_CMD_MASK   0x80          // Máscara para identificar comando (bit más significativo)
#define VAMP_JOIN_REQ   0x81
#define VAMP_JOIN_ACK   0x82
#define VAMP_PING       0x83
#define VAMP_PONG       0x84

/** VREG Command types
* Estos comandos se utilizan para la comunicación con el VREG (Virtual REGistry).
*/
#define VAMP_GATEWAY_SYNC  	"gateway_sync"	// Sincronización con el VREG
#define VAMP_VREG_UPDATE 	"vreg_update"   // Actualización de estado del dispositivo

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
#define VAMP_ASK 0
#define VAMP_TELL 1

/* Fecha de la última actualización de la tabla en UTC */
#define VAMP_TABLE_INIT_TSMP "2020-01-01T00:00:00Z"


// ======================== CALLBACK VREG ========================

// Constantes para métodos HTTP
#define VAMP_HTTP_GET  0
#define VAMP_HTTP_POST 1
// Constantes para radio
#define VAMP_RADIO_READ  0
#define VAMP_RADIO_WRITE 1

/** @brief Callback function type for HTTP communication with VREG
 * 
 * HTTP-specific callback that handles communication with VREG server.
 * The callback should send the data using the specified HTTP method and return the response.
 * 
 * @param endpoint_url VREG endpoint URL (e.g., "https://vreg.example.com/sync")
 * @param method HTTP method: VAMP_HTTP_GET or VAMP_HTTP_POST
 * @param data Buffer containing request data or response data (modified by function)
 * @param data_size Size of data buffer
 * @return bool true on success, false on failure
 */
typedef bool (* vamp_internet_callback_t)(const char * endpoint_url, uint8_t method, char * data, size_t data_size);


/** @brief Callback function type for radio communication
 * 
 * Radio-specific callback that handles communication with devices over the radio.
 * The callback should send the data using the specified method and return true on success.
 * 
 * @param rf_id RF_ID of the device to communicate with
 * @param method HTTP mode: VAMP_RADIO_READ or VAMP_RADIO_WRITE
 * @param data Buffer containing request data or response data
 * @param len Length of data buffer
 * @return bool true on success, false on failure
 */
typedef bool (* vamp_wsn_callback_t)(const char * rf_id, uint8_t mode, uint8_t * data, size_t len);

/* Aqui ponemos el callback para la otra interfase */


/** @brief Register VREG communication callback
 * 
 * @param comm_cb Callback function for VREG communication
 */
void vamp_set_callbacks(vamp_internet_callback_t comm_cb, vamp_wsn_callback_t radio_cb);


/** 
 * @brief Initialize VAMP system with callback and server configuration
 * 
 * @param vamp_http_callback Callback function for HTTP communication
 * @param vamp_vreg_url VREG server URL
 * @param vamp_gw_id Gateway ID string
 */
void vamp_gw_init(vamp_internet_callback_t vamp_http_callback, vamp_wsn_callback_t vamp_radio_callback, const char * vamp_vreg_url, const char * vamp_gw_id);

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

// ======================== FUNCIONES VAMP TABLES ========================

/** @brief Get device identifier by RF_ID
 * 
 * @param rf_id RF_ID to search for
 * @param identifier Buffer to store the identifier (must be at least 8 bytes)
 * @return bool true if found, false if not found
 */
bool vamp_get_device_id(const uint8_t* rf_id, char* identifier);

/** @brief Assign table index to RF_ID
 * 
 * @param rf_id RF_ID to assign index to
 * @return uint8_t Assigned table index, 255 if failed
 */
uint8_t vamp_assign_device_index(const uint8_t* rf_id);

/** @brief Add/update device in VAMP table
 * 
 * @param rf_id RF_ID of the device
 * @return bool true on success, false on failure
 */
bool vamp_add_device(const uint8_t* rf_id);

/** @brief Remove device from VAMP table
 * 
 * @param rf_id RF_ID of the device to remove
 * @return bool true on success, false on failure
 */
bool vamp_remove_device(const uint8_t* rf_id);

/** @brief Find device entry by RF_ID
 * 
 * @param rf_id RF_ID to search for
 * @return vamp_entry_t* Pointer to entry if found, NULL if not found
 */
vamp_entry_t* vamp_find_device(const uint8_t* rf_id);

/** @brief Get device identifier by table index
 * 
 * @param index Table index (0-15)
 * @param identifier Buffer to store the identifier (must be at least 8 bytes)
 * @return bool true if valid index, false otherwise
 */
bool vamp_get_device_id_by_index(int table_index, char* identifier);

// Funciones para manejo de ID compacto (verification + index)
uint8_t vamp_generate_id_byte(const uint8_t* rf_id);
bool vamp_validate_id_byte(uint8_t id_byte, const uint8_t* rf_id);
uint16_t vamp_get_port_by_id_byte(uint8_t id_byte);
vamp_entry_t* vamp_find_device_by_id_byte(uint8_t id_byte);

/** @brief Clean up expired entries
 * 
 * Removes expired entries from VAMP table
 */
void vamp_cleanup_expired(void);

/** @brief Clear a specific entry in VAMP table
 * Clears a single entry efficiently
 */
void vamp_clear_entry(int index);


/** @brief Update/Initialize VAMP tables with optional VREG synchronization
 * 
 */
void vamp_table_update(void);

#endif // _VAMP_H_