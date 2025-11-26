/**
 * 
 * 
 * 
 *  
 */
#include "vamp_callbacks.h"
#include "vamp.h"
#include "vamp_gw.h"
#include "vamp_client.h"

/* Identificador local del WSN */
static uint8_t local_wsn_addr[VAMP_ADDR_LEN] = VAMP_NULL_ADDR;
//static uint8_t gw_wsn_addr[VAMP_ADDR_LEN] = VAMP_BROADCAST_ADDR;


/* Default settings */
static uint8_t vamp_settings = VAMP_RMODE_A; // Configuración de modo de recepción por defecto


uint8_t wsn_buff[VAMP_MAX_PAYLOAD_SIZE];

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

/* uint8_t * vamp_get_gw_addr(void){
	return gw_wsn_addr;
}

void vamp_set_gw_addr(uint8_t * addr){
	memcpy(gw_wsn_addr, addr, VAMP_ADDR_LEN);
} */

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

/* ----------------------------- NRF24L01 ------------------------------ */

#ifdef  __has_include
	#if __has_include(<RF24.h>)
		#define RF24_AVAILABLE
	#endif
#endif


#ifdef RF24_AVAILABLE
#include <SPI.h>
#include <RF24.h>

// Configuración de pines para NRF24L01
#ifdef ARDUINO_ARCH_ESP8266
	#define WSN_CE_PIN D4
	#define WSN_CSN_PIN D8
#endif
#ifdef ARDUINO_ARCH_ESP32
	#define WSN_CE_PIN 5
	#define WSN_CSN_PIN 15
#endif
#ifdef ARDUINO_AVR_NANO
	#define WSN_CE_PIN 9
	#define WSN_CSN_PIN 10
#endif

static uint8_t wsn_ce_pin = WSN_CE_PIN;
static uint8_t wsn_csn_pin = WSN_CSN_PIN;

RF24 wsn_radio; // CE, CSN pins


/** @todo HAY QUE IMPLEMENTARLO PARA QUE USE EL MECAMISMO DE ACK NATIVO DEL NRF24
 * PUES ES MAS EFICIENTE ENERGETICAMENTE !!!!
 */

bool nrf_init(uint8_t ce_pin, uint8_t csn_pin) {

	if (wsn_radio.begin(ce_pin, csn_pin)) {
		// Configuración mínima necesaria para funcionar
		wsn_radio.enableDynamicPayloads(); 	// NECESARIO para getDynamicPayloadSize()
		wsn_radio.disableAckPayload();		// Sin payload en ACKs
		wsn_radio.setAutoAck(false);		// DESHABILITAR ACKs completamente

		/* ✅ Pipe 0 direccion local */
		wsn_radio.openReadingPipe(0, local_wsn_addr);
		/* ✅ Pipe 1 acepta dirección completa diferente (broadcast) */
		uint8_t broadcast_addr[5] = VAMP_BROADCAST_ADDR;
		wsn_radio.openReadingPipe(1, broadcast_addr);
		
		wsn_radio.setAutoAck(1, false);
		wsn_radio.flush_rx();

		#ifdef VAMP_DEBUG
		Serial.println("NRF24 ready");
		#endif /* VAMP_DEBUG */

		/* Si es un gateway siempre debera estar escuchando */
		if (vamp_get_settings() & VAMP_RMODE_B) {
			// Modo siempre escucha
			wsn_radio.startListening(); // Modo siempre escucha
			#ifdef VAMP_DEBUG
			Serial.println("and listening");
			#endif /* VAMP_DEBUG */
			return true; // Éxito - salir de la función
		} 
			
		wsn_radio.stopListening(); // Modo bajo consumo
		return true; // Éxito - salir de la función
	}
	return false; // Fallo - salir de la función
}

void nrf_set_address(uint8_t * rf_id) {
	// Establecer dirección local del pipe de lectura
	wsn_radio.openReadingPipe(0, rf_id);
}


uint8_t nrf_ask(void) {

	/* Recibiendo datos desde el nRF24 */
	if (!wsn_radio.available()) {
		return 0;
	}
	
	/* Leer datos del nRF24 */
	uint8_t bytes_read = wsn_radio.getDynamicPayloadSize();//hay un getPayloadsize??
	if (!bytes_read)
		return 0;
	if (bytes_read > VAMP_MAX_PAYLOAD_SIZE) 
		bytes_read = VAMP_MAX_PAYLOAD_SIZE;
	wsn_radio.read(wsn_buff, bytes_read);

	return bytes_read; // Éxito al recibir datos
}

/** 
 * Escuchar en una ventana por si hay alguna solicitud o un simple ACK
 * @return 	Número de bytes leídos si hay datos disponibles, 
 * 			0 en caso contrario
 * 			VAMP_MAX_PAYLOAD_SIZE + 1 si se recibe un ACK
 * @note	Esta función abre una ventana de escucha y espera por datos.
 */
uint8_t nrf_listen_window(void) {
	
	wsn_radio.flush_rx();
	wsn_radio.startListening();

	uint8_t len = 0;

	uint32_t start_time = millis();
	while ((millis() - start_time) < VAMP_ANSW_TIMEOUT && !len) {
		len = nrf_ask();
	}

	wsn_radio.stopListening();

	/* Si len == 1 probablemente se recibió un ACK */
	if (len == 1) {
		if (wsn_buff[0] == (VAMP_ACK | VAMP_IS_CMD_MASK)) {
			len = VAMP_MAX_PAYLOAD_SIZE + 1;
		}
	}

	return len; // Timeout - no respuesta
}

bool nrf_tell(uint8_t * dst_addr, size_t len) {

	wsn_radio.openWritingPipe(dst_addr);

	// Enviar datos
	if (!wsn_radio.write(wsn_buff, len)){  // Sin tercer parámetro = no ACK
		return false;
	}
	return true; // Éxito al enviar datos		
}

uint8_t nrf_comm(uint8_t * dst_addr, size_t len) {

	/* Verificar que el chip está conectado */
	if (!wsn_radio.isChipConnected()) {
		#ifdef VAMP_DEBUG
		Serial.println("rf24 out");
		#endif /* VAMP_DEBUG */
		return 0;
	}

	/* Mode ASK */
	if (!dst_addr) {

		len = 0;
		/* Si está en modo bajo consumo, hay que abrir una ventana de
			escucha. Esto exige un mecanismo de sincronización que no es
			parte de estas funciones */
		if(vamp_get_settings() & VAMP_RMODE_A){
			/* Encender el chip */
			wsn_radio.powerUp();
			/* ... abrir ventana de escucha */
			len = nrf_listen_window();
			/* Apagar el chip */
			wsn_radio.powerDown();
		} else {
			// Modo siempre escucha, no hay que abrir ventana
			len = nrf_ask();
		}

		return len; // Retornar el número de bytes leídos
	}

	/* Mode TELL */
	if (len) {

		/** @todo este mecanismo que parece logico tiene problemas, por ejemplo:
		 * el ACK payload puede sustituir la ventana de escucha
		 * que pasa cuando se envia sin esperar ACK?
		 * hay que unificar las formas de respuestas de la funcion macro pues puede pasar:
		 * ACK simple (sin payload)
		 * ACK con payload
		 * ACK con payload y ventana
		 * No ACK
		 * y esto no esta bien diferenciado
		 */
		if(vamp_get_settings() & VAMP_RMODE_B) {
			/* Modo siempre escucha asi que que dejar de escuchar */
			wsn_radio.stopListening();
			/* Enviar datos */
			len = (uint8_t)nrf_tell(dst_addr, len);
			/* Volver a escuchar */
			wsn_radio.startListening();

			return len;

		}

		/* Modo bajo consumo, esperamos respuesta */
		if (vamp_get_settings() & VAMP_RMODE_A) {

			/* Encender el chip */
			wsn_radio.powerUp();
			if (nrf_tell(dst_addr, len)) {
				/* Si se envió correctamente, abrir ventana de escucha */
				len = nrf_listen_window();
			} else {
				/* Si hubo un error al enviar, pues se retorna un 0 */
				len = 0;
			}
			
			/* Apagar el chip */
			wsn_radio.powerDown();
			return len; // Retornar el número de bytes leídos
		}
	}
	return 0; //Aqui no se hizo nada porque no se cumplio ninguna de las condiciones
}


#endif /* RF24_AVAILABLE */

/* ----------------------------- /RF24_AVAILABLE ----------------------------- */





/* ------------------------------- wsn --------------------------------- */


void vamp_wsn_spi_config(uint8_t ce_pin, uint8_t csn_pin) {
	// Configurar pines para NRF24L01
	wsn_ce_pin = ce_pin;
	wsn_csn_pin = csn_pin;
}

bool vamp_wsn_init(const uint8_t * wsn_addr) {

	/* Copiar el ID del cliente a la dirección local */
	memcpy(local_wsn_addr, wsn_addr, VAMP_ADDR_LEN);

	#ifdef RF24_AVAILABLE
	return nrf_init(wsn_ce_pin, wsn_csn_pin);
	#endif // RF24_AVAILABLE

}

/* Callback para TELL */
uint8_t vamp_wsn_comm(uint8_t * dst_addr, uint8_t * data, size_t len) {
	/* Verificar que no sea nulo */
	if (!data || !len || !dst_addr) {
		return 0;
	}

	/* Copiar datos al buffer de lectura/escritura */
	memcpy(wsn_buff, data, len);

	#ifdef VAMP_DEBUG
	Serial.print("wsn tell: ");
	vamp_debug_msg(wsn_buff, len);
	#endif /* VAMP_DEBUG */

	#ifdef RF24_AVAILABLE
	len = nrf_comm(dst_addr, len);
	#else //#elif en caso de otras arquitecturas
	len = 0; //y un else final por falta de soporte
	#endif

	/* Si se recibió un mensaje se copia al buffer de datos */
	/** @todo el 1 en el tamaño del payload significaria un ACK para el modo A
	 * y un simplemente enviado en modo B, esto hay que implementarlo
	 */
	if(len <= (VAMP_MAX_PAYLOAD_SIZE) && len > 1) {
		memcpy(data, wsn_buff, len);

		#ifdef VAMP_DEBUG
		Serial.print("wsn tell resp: ");
		vamp_debug_msg(data, len);
		#endif /* VAMP_DEBUG */

	}
	/* De lo contrario, o no se recibió un mensaje o se recibió un ACK */
	return len;
}

/* Callback para ASK */
uint8_t vamp_wsn_comm(uint8_t * data, size_t len) {
	/* Verificar que no sea nulo */
	if (!data || !len) {
		return 0;
	}

	#ifdef RF24_AVAILABLE
	len = nrf_comm(NULL, len);
	#else //#elif en caso de otras arquitecturas
	len = 0; //y un else final por falta de soporte
	#endif

	if (len) {
		/* Si se recibió un mensaje, copiarlo al buffer de datos */
		memcpy(data, wsn_buff, len);

		#ifdef VAMP_DEBUG
		Serial.print("wsn ask: ");
		vamp_debug_msg(data, len);
		#endif /* VAMP_DEBUG */
	}

	return len;

}



/* ----------------------------- /wsn --------------------------------- */




/* -----------------------------  ESP8266 --------------------------------- */

#if defined(ARDUINO_ARCH_ESP8266)

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>


/* ----------------------------- WiFi --------------------------------- */

// Configuración de IP Estática
IPAddress staticIP(10, 1, 111, 198);          // IP estática deseada para el ESP8266
IPAddress gateway(10, 1, 111, 1);             // Dirección IP del router/gateway
IPAddress subnet(255, 255, 255, 0);           // Máscara de subred
IPAddress dns1(193, 254, 231, 2);             // DNS primario (Google)
IPAddress dns2(193, 254, 230, 2);             // DNS secundario (Google)

// Configuración WiFi - CAMBIAR ESTOS VALORES
#define WIFI_SSID "TP-LINK_B06F78" 
#define WIFI_PASSWORD "tplink.cp26"

/* Timeout para conexión WiFi (segundos) */
#define WIFI_TIMEOUT 		30
#define RECONNECT_DELAY 	1000              // Delay para reconexión WiFi (ms)

// Configuración HTTPS para comunicación con VAMP Registry y endpoints
#define HTTPS_TIMEOUT 		10000              // Timeout para requests HTTPS (ms)
#define HTTPS_USER_AGENT 	"VAMP-Gateway/1.0" // User agent para requests

/* -----------------------------  /WiFi --------------------------------- */


/* Objetos globales para comunicación HTTPS (reutilizables) */
static WiFiClientSecure https_client;
static HTTPClient https_http;

/* Inicializar cliente HTTPS */
bool esp8266_init(){

	// Configurar IP estático antes de conectar
    WiFi.config(staticIP, gateway, subnet, dns1, dns2);
	WiFi.mode(WIFI_STA);
	WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
	
	// Esperar conexión (timeout de 30 segundos)
	int timeout = WIFI_TIMEOUT;
	while (WiFi.status() != WL_CONNECTED && timeout > 0) {
		delay(1000);
		#ifdef VAMP_DEBUG
		Serial.print(".");
		#endif /* VAMP_DEBUG */
		timeout--;
	}
	
	if (WiFi.status() == WL_CONNECTED) {
		#ifdef VAMP_DEBUG
		Serial.println();
		Serial.print("WiFi conectado! IP: ");
		Serial.println(WiFi.localIP());
		Serial.println("WiFi inicializado correctamente");
		#endif /* VAMP_DEBUG */
		return true;
	} else {
		#ifdef VAMP_DEBUG
		Serial.println();
		Serial.println("Error: No se pudo conectar a WiFi");
		Serial.println("Error al inicializar WiFi");
		#endif /* VAMP_DEBUG */
		return false;
	}
}


// Función para verificar y mantener la conexión WiFi
bool esp8266_check_conn() {
	// Si la conexión WiFi está activa, todo bien
	if (WiFi.status() == WL_CONNECTED) {
		return true;
	}
	#ifdef VAMP_DEBUG
	Serial.println("Conexión WiFi perdida, intentando reconectar...");
	#endif /* VAMP_DEBUG */
	/* Intentar reconectar usando esp8266_init() */
	esp8266_init();
	if (WiFi.status() == WL_CONNECTED) {
		#ifdef VAMP_DEBUG
		Serial.println("Reconexión WiFi exitosa");
		#endif /* VAMP_DEBUG */
		return true;
	} else {
		#ifdef VAMP_DEBUG
		Serial.println("Fallo en reconexión WiFi, reintentando en próximo ciclo");
		#endif /* VAMP_DEBUG */
		delay(RECONNECT_DELAY);
		return false;
	}
}

// Función para enviar datos por HTTPS
bool esp8266_https(const vamp_profile_t * profile, char * data, size_t data_size) {
	if (!esp8266_check_conn()) {
		#ifdef VAMP_DEBUG
		Serial.println("Error: WiFi no conectado");
		#endif /* VAMP_DEBUG */
		return false;
	}
	
	/* Configurar cliente HTTPS (desactivar verificación SSL para testing) */
	https_client.setInsecure(); // Solo para desarrollo - en producción usar certificados

	/* Configurar HTTP client - reutilizar conexión si es posible */
	https_http.begin(https_client, profile->endpoint_resource);
	https_http.setTimeout(HTTPS_TIMEOUT);
	https_http.setUserAgent(HTTPS_USER_AGENT);

	/* Añadir headers personalizados si hubiera */
	if (profile->protocol_params && profile->protocol_params[0] != '\0') {
		const char * p = profile->protocol_params;
		while (*p) {
			// Encontrar fin de par header
			const char *sep = strchr(p, ':');
			if (!sep) break;
			const char *end = strchr(sep + 1, ',');
			if (!end) end = p + strlen(p);
			// Extraer nombre y valor
			int name_len = (int)(sep - p);
			int val_len = (int)(end - (sep + 1));
			if (name_len > 0 && val_len > 0) {
				String hname = String(p).substring(0, name_len);
				String hval = String(sep + 1).substring(0, val_len);
				hname.trim();
				hval.trim();
				if (hname.length() > 0) {
					https_http.addHeader(hname, hval);
				}
			}
			if (*end == ',') end++;
			p = end;
		}
	}
	
	int httpResponseCode = -1;
	
	
	switch (profile->method) {
		/* GET */
		case VAMP_HTTP_METHOD_GET:
			httpResponseCode = https_http.GET();
			break;
		case VAMP_HTTP_METHOD_POST:
			httpResponseCode = https_http.POST((uint8_t*)data, data_size);
			break;
		default:
			break;
	}
	
	// Procesar respuesta
	if (httpResponseCode > 0) {
		String response = https_http.getString();

		if(response.length() == 0 || response.length() >= VAMP_IFACE_BUFF_SIZE) {
			/* Buffer nulo o demasiado grande */
			#ifdef VAMP_DEBUG
			Serial.println("Error: Respuesta inválida");
			#endif /* VAMP_DEBUG */
			return false;
		}
		/* Copiar respuesta al buffer proporcionado */
		sprintf(data, "%s", response.c_str());

		#ifdef VAMP_DEBUG
		Serial.print("rspta de ");
		Serial.print(profile->endpoint_resource);
		Serial.print(" - Código: ");
		Serial.println(httpResponseCode);
		#endif /* VAMP_DEBUG */
		https_http.end();
		
		return true; // Éxito
	
	} else {
		#ifdef VAMP_DEBUG
		Serial.print("Error en HTTPS ");
		Serial.print(": ");
		Serial.println(httpResponseCode);
		#endif /* VAMP_DEBUG */
		https_http.end(); // Cerrar conexión solo en caso de error
		return false;
	}
}



#endif // ARDUINO_ARCH_ESP8266




/* -----------------------------  /ESP8266 --------------------------------- */

/* ----------------------------- gateway --------------------------------- */

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
	profile.protocol_params = NULL;

	#if defined(ARDUINO_ARCH_ESP8266)
	len = (uint8_t)esp8266_https(&profile, data, len);
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
		return esp8266_https(profile, data, len);
	#endif

	return 0;
}