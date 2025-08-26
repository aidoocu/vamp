/** 
 * 
 * definiendo procesos de comunicación
 * ASK pedir al extremo que reencamine datos al endpoint
 * TELL enviar datos al extremo
 * POLL sondear si el extremo tiene una respuesta para, o simplemente si esta disponible. pudiera funcionar como un keep-alive
 *
 * READ leer datos desde la interface de red
 * WRITE enviar datos a la interface de red
 */

#ifndef VAMP_CALLBACKS_H
#define VAMP_CALLBACKS_H

#include <Arduino.h>
#include "vamp_config.h"

// Forward declarations to avoid circular dependencies
// Only include what we absolutely need in the header

#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#endif

/**
 * @brief Configure the SPI pins for the WSN
 * @note This function HAVE TO BE called before initializing the WSN.
 * @param ce_pin Chip Enable pin
 * @param csn_pin Chip Select Not pin
 */
void vamp_wsn_spi_config(uint8_t ce_pin, uint8_t csn_pin);


/**
 * @brief Get/Set the local RF_ID
 * @return Pointer to the local RF_ID
 */
uint8_t * vamp_get_local_wsn_addr(void);
void vamp_set_local_wsn_addr(const uint8_t * wsn_addr);

/**
 * @brief Get/Set/Add the VAMP settings
 * @return VAMP settings
 */
uint8_t vamp_get_settings(void);
void vamp_set_settings(uint8_t settings);
void vamp_add_settings(uint8_t settings);


#ifdef VAMP_DEBUG
void vamp_debug_msg(uint8_t * msg, uint8_t len);
#endif /* VAMP_DEBUG */

/* ---------------------------------- wsn ---------------------------------- */


#ifdef  __has_include
	#if __has_include(<RF24.h>)
		#define RF24_AVAILABLE
	#endif
#endif


/**
 * @brief Funtion for init WSN communication
 * @param rf_id Local RF_ID
 */
bool vamp_wsn_init(const uint8_t * wsn_addr);

/** @brief Function for TELL WSN radio communication
 * 
 * Radio-specific callback that handles communication with devices over the radio.
 * The callback should send the data using the specified method and return true on success.
 * 
 * @param rf_id RF_ID of the device to communicate with on TELL, or NULL for ASK
 * @param data Buffer containing request data or response data
 * @param len Length of data buffer
 * @return If mode is VAMP_TELL, returns 1 on success, 0 on failure.
 *         If mode is VAMP_ASK, returns amount of data received, 0 means no data received.
 */
uint8_t vamp_wsn_send(uint8_t * dst_addr, uint8_t * data, size_t len);


/** @brief Callback para enviar TICKET
 *  @return true si se envió correctamente, false en caso contrario
 */
bool vamp_wsn_send_ticket(uint8_t * dst_addr, uint16_t ticket);

/**
 * @brief Function for ASK WSN radio communication
 * @param data Buffer containing the data from the radio iface
 * @param len Length of data buffer
 * @return Tamaño de los datos recibidos
 */
uint8_t vamp_wsn_recv(uint8_t * data, size_t len);



/* ---------------------------------- gateway ---------------------------------- */

/**
 * @brief Initialize the VAMP interface for internet communication
 * @return true if the interface was initialized successfully, false otherwise
 */
bool vamp_iface_init(void);


/**
 * @brief Function for sending data to the VREG server
 * @param url URL of the VREG resource
 * @param data Data to send, if answer, data will contain the response
 * @param len Length of data buffer
 * @return 
 */
uint8_t vamp_iface_comm(const char * url, char * data, size_t len);

// Forward declare profile type to avoid including vamp_gw.h here
struct vamp_profile_t;

/**
 * @brief Function for sending data to the VREG server
 * @param profile entire profile of the VREG resource
 * @param data  Data to send, if answer, data will contain the response
 * @param len Length of data buffer
 * @return 
 */
uint8_t vamp_iface_comm(const vamp_profile_t * profile, char * data, size_t len);


#endif // VAMP_CALLBACKS_H

