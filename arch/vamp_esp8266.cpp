/** 
 * 
 * 
 * 
 * 
 * 
 */

 

/* -----------------------------  ESP8266 --------------------------------- */

#if defined(ARDUINO_ARCH_ESP8266)

#include "../vamp_gw.h"
#include "../vamp_client.h"
#include "../vamp_callbacks.h"
#include "../lib/vamp_table.h"

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

// Función unificada para enviar datos por HTTP/HTTPS
bool esp8266_http_request(const vamp_profile_t * profile, char * data, size_t data_size) {
	if (!esp8266_check_conn()) {
		#ifdef VAMP_DEBUG
		Serial.println("Error: WiFi no conectado");
		#endif /* VAMP_DEBUG */
		return false;
	}
	
	String full_url = String(profile->endpoint_resource);

	/* Añadir parámetros de consulta si existen */
	if (profile->query_params.count > 0) {
		// Convertir query_params a query string
		char query_buffer[256];
		size_t len = vamp_kv_to_query_string(&profile->query_params, query_buffer, sizeof(query_buffer));
		if (len > 0) {
			full_url += "?";
			full_url += String(query_buffer);
		}
	}

	bool is_https = strncmp(profile->endpoint_resource, "https://", 8) == 0;

	Serial.print("Conectando a: ");
	Serial.println(full_url);
	Serial.print("options count: ");
	Serial.println(profile->protocol_options.count);

	/* Discriminar entre HTTP y HTTPS */
	if (is_https) {
		/* Configurar cliente HTTPS */
		https_client.setInsecure(); // Solo para desarrollo
		https_http.begin(https_client, full_url);
	} else {
		/* Configurar cliente HTTP normal */
		WiFiClient http_client;
		https_http.begin(http_client, full_url);
	}

	https_http.setTimeout(HTTPS_TIMEOUT);
	https_http.setUserAgent(HTTPS_USER_AGENT);

	/* Añadir headers personalizados desde key-value store */
	for (uint8_t i = 0; i < profile->protocol_options.count; i++) {
		const char* key = profile->protocol_options.pairs[i].key;
		const char* value = profile->protocol_options.pairs[i].value;
		
		if (key[0] != '\0' && value[0] != '\0') {
			https_http.addHeader(key, value);
			
			#ifdef VAMP_DEBUG
			Serial.print("Adding header: ");
			Serial.print(key);
			Serial.print(" = ");
			Serial.println(value);
			#endif /* VAMP_DEBUG */
		}
	}
	
	int httpResponseCode = -1;
	
	switch (profile->method) {
		/* GET */
		case VAMP_HTTP_METHOD_GET:
			Serial.print("Enviando GET request ");
			Serial.println(is_https ? "(HTTPS)..." : "(HTTP)...");
			httpResponseCode = https_http.GET();
			break;
		case VAMP_HTTP_METHOD_POST:
			Serial.print("Enviando POST request ");
			Serial.println(is_https ? "(HTTPS)..." : "(HTTP)...");
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
		Serial.print("Error en ");
		Serial.print(is_https ? "HTTPS" : "HTTP");
		Serial.print(": ");
		Serial.println(httpResponseCode);
		#endif /* VAMP_DEBUG */
		https_http.end(); // Cerrar conexión solo en caso de error
		return false;
	}
}

#endif // ARDUINO_ARCH_ESP8266

