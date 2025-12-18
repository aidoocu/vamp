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
		Serial.println("[empty]");
		delay(10);
		return;
	}
	
	len = len - 1;

    for (int i = 0; i < len; i++) {
        printf("%02X:", msg[i]);
    }
	printf("%02X", msg[len]);
    printf("\n");
    delay(10);

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
	delay(10);
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

	/* Si debe ser configurada como estática */
	if (vamp_conf->net.mode == "static") {

		/* Validar que la IP estática esté definida (no 0.0.0.0) */
		IPAddress ip = vamp_conf->net.ip;
		if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
			#ifdef VAMP_DEBUG
			Serial.println("[NET] Error: NET.mode == static pero NET.ip no está definida");
			#endif
			
			/* No tiene sentido continuar sin IP */
			return false;
		}

		/* Preparar valores de red, usar defaults cuando falten */
			IPAddress gateway = vamp_conf->net.gateway;
			IPAddress subnet = vamp_conf->net.subnet;
			IPAddress dns1 = vamp_conf->net.dns1;
			IPAddress dns2 = vamp_conf->net.dns2;

		// Subnet por defecto
		if (subnet[0] == 0 && subnet[1] == 0 && subnet[2] == 0 && subnet[3] == 0) {
			subnet = IPAddress(255,255,255,0);
			#ifdef VAMP_DEBUG
			Serial.println("[NET] Subnet no proporcionada, usando 255.255.255.0");
			#endif
		}

		// Gateway por defecto: misma red que la IP con .1
		if (gateway[0] == 0 && gateway[1] == 0 && gateway[2] == 0 && gateway[3] == 0) {
			gateway = ip;
			gateway[3] = 1;
			#ifdef VAMP_DEBUG
			Serial.print("[NET] Gateway no proporcionado, usando: "); Serial.println(gateway);
			#endif
		}

		// DNS por defecto
		if (dns1[0] == 0 && dns1[1] == 0 && dns1[2] == 0 && dns1[3] == 0) {
			dns1 = gateway;
			#ifdef VAMP_DEBUG
			Serial.print("[NET] DNS1 no proporcionado, usando gateway: "); Serial.println(dns1);
			#endif
		}
		if (dns2[0] == 0 && dns2[1] == 0 && dns2[2] == 0 && dns2[3] == 0) {
			dns2 = IPAddress(8,8,8,8);
			#ifdef VAMP_DEBUG
			Serial.print("[NET] DNS2 no proporcionado, usando 8.8.8.8\n");
			#endif
		}

		esp8266_sta_static_ip(ip, gateway, subnet, dns1, dns2);
	}

	/* Inicializar la interfaz WiFi */
	return esp8266_sta_init(vamp_conf->wifi.ssid.c_str(), vamp_conf->wifi.password.c_str());
	
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