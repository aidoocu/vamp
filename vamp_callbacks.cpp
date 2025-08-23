/**
 * 
 * 
 * 
 *  
 */
#include <Arduino.h>
#include "vamp_callbacks.h"
#include "vamp.h"
#include "vamp_gw.h"

//#include "lib/vamp_kv.h"
#include "lib/vamp_table.h"

#ifdef RF24_AVAILABLE
#include "arch/vamp_nrf24.h"
#endif // RF24_AVAILABLE

/* Identificador local del WSN */
static uint8_t local_wsn_addr[VAMP_ADDR_LEN] = VAMP_NULL_ADDR;
//static uint8_t gw_wsn_addr[VAMP_ADDR_LEN] = VAMP_BROADCAST_ADDR;


/* Default settings */
static uint8_t vamp_settings = VAMP_RMODE_A; // Configuración de modo de recepción por defecto


uint8_t * vamp_get_local_wsn_addr(void){
	return local_wsn_addr;
}

void vamp_set_local_wsn_addr(uint8_t * addr){
	/* Copiar el ID del cliente a la dirección local */
	memcpy(local_wsn_addr, addr, VAMP_ADDR_LEN);
}

/** @todo Cuando se cambian los settings habri que reiniciar el radio!! */

void vamp_set_settings(uint8_t settings) {
	/* Configurar los ajustes de VAMP */
	vamp_settings = settings;
}

uint8_t vamp_get_settings(void) {
	/* Obtener los ajustes de VAMP */
	return vamp_settings;
}

void vamp_add_settings(uint8_t settings) {
	/* Añadir ajustes de VAMP */
	vamp_settings |= settings;
}

#ifdef VAMP_DEBUG
void vamp_debug_msg(uint8_t * msg, uint8_t len) {
	
	for (uint8_t i = 0; i < len; i++) {
		Serial.print(msg[i], HEX);
		if (i < len - 1) {
			Serial.print(":");
		}
	}
	Serial.println();
}
#endif /* VAMP_DEBUG */



/* ------------------------------- wsn --------------------------------- */


// Configuración de pines para NRF24L01
#ifdef ARDUINO_ARCH_ESP8266
	#define WSN_CE_PIN 2  // D4 = GPIO2
	#define WSN_CSN_PIN 15 // D8 = GPIO15
#endif
#ifdef ARDUINO_ARCH_ESP32
	#define WSN_CE_PIN 5
	#define WSN_CSN_PIN 15
#endif
#ifdef ARDUINO_AVR_NANO
	#define WSN_CE_PIN 9
	#define WSN_CSN_PIN 10
#endif
#ifndef WSN_CE_PIN
	#define WSN_CE_PIN 9 // Default value if not defined
#endif
#ifndef WSN_CSN_PIN
	#define WSN_CSN_PIN 10 // Default value if not defined
#endif

static uint8_t wsn_ce_pin = WSN_CE_PIN;
static uint8_t wsn_csn_pin = WSN_CSN_PIN;

void vamp_wsn_spi_config(uint8_t ce_pin, uint8_t csn_pin) {
	// Configurar pines para NRF24L01
	wsn_ce_pin = ce_pin;
	wsn_csn_pin = csn_pin;
}

bool vamp_wsn_init(const uint8_t * wsn_addr) {

	/* Copiar el ID del cliente a la dirección local */
	memcpy(local_wsn_addr, wsn_addr, VAMP_ADDR_LEN);

	#ifdef RF24_AVAILABLE
	return nrf_init(wsn_ce_pin, wsn_csn_pin, local_wsn_addr);
	#else
	return false; // NRF24 not available
	#endif // RF24_AVAILABLE

}

/* Callback para TELL/ASK */
uint8_t vamp_wsn_send(uint8_t * dst_addr, uint8_t * data, size_t len) {
	/* Verificar que no sea nulo */
	if (!data || !len || !dst_addr) {
		return 0;
	}

	#ifdef VAMP_DEBUG
	Serial.print("wsn tell: ");
	vamp_debug_msg(data, len);
	#endif /* VAMP_DEBUG */

	#ifdef RF24_AVAILABLE
	len = nrf_comm(dst_addr, data, len);
	#else //#elif en caso de otras arquitecturas
	len = 0; //y un else final por falta de soporte
	#endif

	#ifdef VAMP_DEBUG
	if (vamp_get_settings() & VAMP_RMODE_A) {
		Serial.print("wsn tell resp: ");
		vamp_debug_msg(data, len);
	}
	#endif /* VAMP_DEBUG */

	/* De lo contrario, o no se recibió un mensaje o se recibió un ACK */
	return len;
}

bool vamp_wsn_send_ack(uint8_t * dst_addr, uint8_t ticket) {
	/* Verificar que no sea nulo */
	if (!dst_addr) {
		return false;
	}

	#ifdef VAMP_DEBUG
	Serial.print("wsn send ack: ");
	#endif /* VAMP_DEBUG */

	uint8_t send[] = { VAMP_ACK | VAMP_IS_CMD_MASK, ticket };

	#ifdef RF24_AVAILABLE
	nrf_comm(dst_addr, send, sizeof(send));
	#endif

	return true;
}

/* Callback para READ */
uint8_t vamp_wsn_recv(uint8_t * data, size_t len) {
	/* Verificar que no sea nulo */
	if (!data || !len) {
		return 0;
	}

	#ifdef RF24_AVAILABLE
	len = nrf_comm(NULL, data, len);
	#else //#elif en caso de otras arquitecturas
	len = 0; //y un else final por falta de soporte
	#endif
	
	#ifdef VAMP_DEBUG
	if (len) {
		Serial.print("wsn recv: ");
		vamp_debug_msg(data, len);
	}
	#endif /* VAMP_DEBUG */

	return len;

}

/* ----------------------------- /wsn --------------------------------- */



/* ----------------------------- gateway --------------------------------- */


#if defined(ARDUINO_ARCH_ESP8266)
#include "arch/vamp_esp8266.h"
#endif

bool vamp_iface_init(void) {

	#if defined(ARDUINO_ARCH_ESP8266)
	return esp8266_init();
	#endif // ARDUINO_ARCH_ESP8266

	return 0;

}

/* Callback para comunicación con el servidor VREG */
uint8_t vamp_iface_comm(const char * url, char * data, size_t len) {
	/* Verificar que no sea nulo */
	if (!url || !data) {
		return 0;
	}

	/* crear perfil temporal para comunicación */
	uint8_t url_len = strlen(url);
	vamp_profile_t profile = {};

	/* Reservar memoria para el endpoint antes de usarlo */
	profile.endpoint_resource = (char*)malloc(url_len + 1);
	if (!profile.endpoint_resource) {
		#ifdef VAMP_DEBUG
		Serial.println("Error: No se pudo reservar memoria para endpoint");
		#endif /* VAMP_DEBUG */
		return 0;
	}
	
	memcpy(profile.endpoint_resource, url, url_len);
	/* Asegurar terminación nula */
	profile.endpoint_resource[url_len] = '\0';

	len ? profile.method = VAMP_HTTP_METHOD_POST : profile.method = VAMP_HTTP_METHOD_GET;
	vamp_kv_init(&profile.protocol_options);
	vamp_kv_init(&profile.query_params);

	#if defined(ARDUINO_ARCH_ESP8266)
	/* Usar función unificada para HTTP/HTTPS */
	len = (uint8_t)esp8266_http_request(&profile, data, len);
	#else //#elif en caso de otras arquitecturas
	/* Se devuelve 0 en caso de no tener ninguna arquitectura definida */
	len = 0;
	#endif // ARDUINO_ARCH_ESP8266

	/* Liberar memoria reservada siempre */
	free(profile.endpoint_resource);

	return len; // Implementar la comunicación con el servidor VREG

}

/* Internal helper that uses explicit method and params */
uint8_t vamp_iface_comm(const vamp_profile_t * profile, char * data, size_t len) {
	if (!profile || !data) {
		return 0;
	}

	#if defined(ARDUINO_ARCH_ESP8266)
	/* Usar función unificada para HTTP/HTTPS */
	return esp8266_http_request(profile, data, len);
	#endif

	return 0;
}