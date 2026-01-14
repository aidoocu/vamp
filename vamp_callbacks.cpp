/**
 * 
 * 
 * 
 *  
 */
#include <Arduino.h>
#include <cstring>
#include "vamp_callbacks.h"
#include "vamp.h"
#include "vamp_gw.h"

//#include "lib/vamp_kv.h"
#include "lib/vamp_table.h"

#ifdef RF24_AVAILABLE
#include "arch/iface/vamp_nrf24.h"
#endif // RF24_AVAILABLE


/* Default settings */
static uint8_t vamp_settings = VAMP_RMODE_A; // Configuración de modo de recepción por defecto

/* Obtener la dirección local del WSN */
uint8_t * vamp_get_local_wsn_addr(void){
	
	#ifdef RF24_AVAILABLE
	return nrf_get_local_wsn_addr();
	#else
	return NULL; // NRF24 not available
	#endif // RF24_AVAILABLE
	
}

/* Establecer la dirección local del WSN */
void vamp_set_local_wsn_addr(const uint8_t * wsn_addr){

	if (!wsn_addr) {
		return;
	}

	#ifdef RF24_AVAILABLE
	nrf_set_local_wsn_addr((uint8_t *)wsn_addr);
	#endif // RF24_AVAILABLE

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

	if (len == 0 || msg == NULL) {
		printf("[empty]\n");
		return;
	}
	
	len = len - 1;

    for (int i = 0; i < len; i++) {
        printf("%02X:", msg[i]);
    }
	printf("%02X\n", msg[len]);

}
#endif /* VAMP_DEBUG */



/* ------------------------------- wsn --------------------------------- */


// Configuración de pines para NRF24L01
/* !!!! Esta configuración puede variar según la placa utilizada !!!! */
#ifdef ARDUINO_ARCH_ESP8266
	#define WSN_CE_PIN 0  // D3 = GPIO0
	#define WSN_CSN_PIN 2 // D4 = GPIO2
#endif
#ifdef ARDUINO_ARCH_ESP32
	#define WSN_CE_PIN 5
	#define WSN_CSN_PIN 15
#endif
#ifdef ARDUINO_AVR_NANO
	#define WSN_CE_PIN 9
	#define WSN_CSN_PIN 10
#endif
//Esta es la configuración para la placa Pro Mini == Placa mote
#ifdef ARDUINO_AVR_PRO
	#define WSN_CE_PIN 9
	#define WSN_CSN_PIN 10
#endif
#ifdef MOTE_IDOS_BOARD
	#ifndef WSN_CSN_PIN
	#define WSN_CE_PIN 9
	#endif
	#ifndef WSN_CSN_PIN
	#define WSN_CSN_PIN 10
	#endif
#endif
#ifndef WSN_CE_PIN
	#define WSN_CE_PIN 9 // Default value if not defined
#endif
#ifndef WSN_CSN_PIN
	#define WSN_CSN_PIN 10 // Default value if not defined
#endif

/* Inicializar NRF24L01 */
bool vamp_wsn_init(uint8_t * wsn_addr, uint8_t ce_pin, uint8_t csn_pin) {

	#ifdef RF24_AVAILABLE
	return nrf_init(ce_pin, csn_pin, wsn_addr);
	#else
	return false; // NRF24 not available
	#endif // RF24_AVAILABLE

}

bool vamp_wsn_init(uint8_t * wsn_addr) {
	return vamp_wsn_init(wsn_addr, WSN_CE_PIN, WSN_CSN_PIN);
}

/* Callback para TELL/ASK */
uint8_t vamp_wsn_send(uint8_t * dst_addr, uint8_t * data, size_t len) {
	/* Verificar que no sea nulo */
	if (!data || !len || !dst_addr) {
		return 0;
	}

	#ifdef VAMP_DEBUG
	printf("[WSN] tell: ");
	vamp_debug_msg(data, len);
	#endif /* VAMP_DEBUG */

	#ifdef RF24_AVAILABLE
	len = nrf_comm(dst_addr, data, len);
	#else //#elif en caso de otras arquitecturas
	len = 0; //y un else final por falta de soporte
	#endif

	#ifdef VAMP_DEBUG
	if (vamp_get_settings() & VAMP_RMODE_A) {
		printf("[WSN] resp: %d bytes - ", len);
		vamp_debug_msg(data, len);
	}
	#endif /* VAMP_DEBUG */

	/* De lo contrario, o no se recibió un mensaje o se recibió un ACK */
	return len;
}

bool vamp_wsn_send_ticket(uint8_t * dst_addr, uint16_t ticket) {
	/* Verificar que no sea nulo */
	if (!dst_addr) {
		return false;
	}

	#ifdef VAMP_DEBUG
	printf("[WSN] send ticket: %04X\n", ticket);
	#endif /* VAMP_DEBUG */

	uint8_t send[] = { VAMP_TICKET | VAMP_IS_CMD_MASK, (uint8_t)((ticket >> 8) & 0xFF), (uint8_t)(ticket & 0xFF) };

	#ifdef RF24_AVAILABLE
	nrf_comm(dst_addr, send, sizeof(send));
	#endif

	return true;
}

/* Callback para READ */
int8_t vamp_wsn_recv(uint8_t * data, uint8_t len) {

	/* La validación de los datos que pasan por parámetro debe 
	estar contenida en la función nrf_comm() por lo que no debería
	ser necesario validarlos aquí */

	/* Inicializar la longitud de recepción, que debe tener signo
	porque la respuesta tiene signo */
	int8_t recv_len = 0;

	#ifdef RF24_AVAILABLE
	recv_len = nrf_comm(NULL, data, len);
	#else //#elif en caso de otras arquitecturas
	recv_len = 0; //y un else final por falta de soporte
	#endif
	
	#ifdef VAMP_DEBUG
	if (recv_len > 0) {
		printf("[WSN] recv: %d bytes - ", recv_len);
		vamp_debug_msg(data, recv_len);
	}
	#endif /* VAMP_DEBUG */

	return recv_len;

}

/* ----------------------------- /wsn --------------------------------- */



/* ----------------------------- gateway --------------------------------- */


#if defined(ARDUINO_ARCH_ESP8266)
#include "arch/iface/vamp_esp8266.h"
#endif

bool vamp_iface_init(const gw_config_t * vamp_conf) {

	if (!vamp_conf) {
		return false;
	}

	#if defined(ARDUINO_ARCH_ESP8266)
	/* Inicializar la interfaz WiFi - esto conecta y configura */
	if(esp8266_sta_init(vamp_conf)){
		return true;
	} 
	
	return false;

	
	#endif // ARDUINO_ARCH_ESP8266

	return 0;

}

/* Callback para comunicación con el servidor VREG */
/* Revisar porque para un GET no hay data */
uint8_t vamp_iface_comm(const uint8_t method, const char * url, char * data, size_t len) {
	/* Verificar que no sea nulo */
	if (!url || !data) {
		return 0;
	}

	/* crear perfil temporal para comunicación */
	vamp_profile_t profile = {};

	/* Reservar memoria para el endpoint antes de usarlo */
	profile.endpoint_resource = (char*)url;
	if (!profile.endpoint_resource) {
		#ifdef VAMP_DEBUG
		printf("[CALLBACK] Error: No se pudo reservar memoria para endpoint\n");
		#endif /* VAMP_DEBUG */
		return 0;
	}
	
	/* Configurar método HTTP */
	switch (method) {
		case VAMP_HTTP_METHOD_GET:
			profile.method = VAMP_HTTP_METHOD_GET;
			break;
		case VAMP_HTTP_METHOD_POST:
			profile.method = VAMP_HTTP_METHOD_POST;
			break;
		default:
			#ifdef VAMP_DEBUG
			printf("[CALLBACK] Error: Método HTTP no soportado\n");
			#endif /* VAMP_DEBUG */
			return 0;
	}

	/* Inicializar stores key-value */
	vamp_kv_init(&profile.protocol_options);
	vamp_kv_init(&profile.query_params);

	#if defined(ARDUINO_ARCH_ESP8266)
	/* Usar función unificada para HTTP/HTTPS */
	len = (uint8_t)esp8266_http_request(&profile, data, len);
	#else //#elif en caso de otras arquitecturas
	/* Se devuelve 0 en caso de no tener ninguna arquitectura definida */
	len = 0;
	#endif // ARDUINO_ARCH_ESP8266

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