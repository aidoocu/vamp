/** 
 * 
 * 
 * 
 * 
 * 
 */

 

/* -----------------------------  ESP8266 --------------------------------- */

#if defined(ARDUINO_ARCH_ESP8266)

/* ToDo esto asi esta feo */
#include "../../vamp_gw.h"
#include "../../vamp_client.h"
#include "../../vamp_callbacks.h"
#include "../../lib/vamp_table.h"

#include "../../../hmi/display.h"

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>


/* ----------------------------- WiFi --------------------------------- */

/* Timeout para conexión WiFi (segundos) */
#define WIFI_TIMEOUT 		30
#define RECONNECT_DELAY 	1000              // Delay para reconexión WiFi (ms)

// Configuración HTTPS para comunicación con VAMP Registry y endpoints
#define HTTPS_TIMEOUT 		10000              // Timeout para requests HTTPS (ms)
#define HTTPS_USER_AGENT 	"VAMP-Gateway/1.0" // User agent para requests

static String wifi_ssid_local = "";
static String wifi_password_local = "";

/* -----------------------------  /WiFi --------------------------------- */


/* Objetos globales para comunicación HTTPS (reutilizables) */
static WiFiClientSecure https_client;
static HTTPClient https_http;

/* Inicializar cliente HTTPS */
bool esp8266_conn(void){

	/*  WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 */
	WiFi.mode(WIFI_STA);
	WiFi.begin(wifi_ssid_local.c_str(), wifi_password_local.c_str());
	
	// Esperar conexión (timeout de 30 segundos)
	int timeout = WIFI_TIMEOUT;

	#ifdef VAMP_DEBUG
	printf("[WiFi] Connecting to SSID: %s - ", wifi_ssid_local.c_str());
	delay(10);
	#endif /* VAMP_DEBUG */

	while (WiFi.status() != WL_CONNECTED && timeout > 0) {
		delay(1000);

		#ifdef VAMP_DEBUG
		printf(".");
		#endif /* VAMP_DEBUG */
		
		/** @todo Como esto no es parte de VAMP hay que evaluar sacarlo de aqui o hacerlo
		 * parte de un arch o algo asi
		 */
		/* Hacer parpadear la barra de la wifi para ilustrar que la esta buscando */
		#ifdef OLED_DISPLAY
		if(timeout % 2 == 0) {
			display.fillRect(5, 48, 16, 16, SSD1306_BLACK);
			display.drawBitmap(5, 48, wifi_icon_16x16, 16, 16, SSD1306_WHITE);
			display.display();

			#ifdef VAMP_DEBUG
			printf("w");
			#endif /* VAMP_DEBUG */
		} else {
			display.fillRect(5, 48, 16, 16, SSD1306_BLACK);
			display.drawBitmap(5, 48, wifi_off_icon_16x16, 16, 16, SSD1306_WHITE);
			display.display();

			#ifdef VAMP_DEBUG
			printf(" ");
			#endif /* VAMP_DEBUG */
		}
		#endif	 /* OLED_DISPLAY */

		#ifdef VAMP_DEBUG
		
		#endif

		timeout--;
	
	}
	
	if (WiFi.status() == WL_CONNECTED) {

		#ifdef VAMP_DEBUG
		printf("\n[WiFi] connected! IP: %s\n", WiFi.localIP().toString().c_str());
		delay(10);
		#endif /* VAMP_DEBUG */

		/* Display */
		#ifdef OLED_DISPLAY
		display.fillRect(5, 48, 16, 16, SSD1306_BLACK);
		display.drawBitmap(5, 48, wifi_icon_16x16, 16, 16, SSD1306_WHITE);
		display.display();
		//draw_wifi_signal_bar();
		#endif	 /* OLED_DISPLAY */

		return true;

	} else {

		#ifdef VAMP_DEBUG
		printf("\n[WiFi] failed to connect within %d seconds\n", WIFI_TIMEOUT);
		delay(10);
		#endif /* VAMP_DEBUG */

		/* Display */
		#ifdef OLED_DISPLAY
		display.fillRect(5, 48, 16, 16, SSD1306_BLACK);
		display.drawBitmap(5, 48, wifi_off_icon_16x16, 16, 16, SSD1306_WHITE);
		display.display();
		#endif	 /* OLED_DISPLAY */

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
	Serial.println("[WiFi] Lost connection, attempting to reconnect...");
	delay(10);
	#endif /* VAMP_DEBUG */

	#ifdef OLED_DISPLAY
	/* Limpiar barra de WiFi */
	display.fillRect(0, 0, 3, 48, SSD1306_BLACK);
	/* Mostrar icono de WiFi desconectada */
	display.fillRect(5, 48, 16, 16, SSD1306_BLACK);
	display.drawBitmap(5, 48, wifi_off_icon_16x16, 16, 16, SSD1306_WHITE);
	display.display();
	#endif	 /* OLED_DISPLAY */

	/* Intentar reconectar usando esp8266_conn() */
	esp8266_conn();
	
	if (WiFi.status() == WL_CONNECTED) {
		#ifdef VAMP_DEBUG
		printf("[WiFi] Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
		delay(10);
		#endif /* VAMP_DEBUG */
		return true;
	} else {
		#ifdef VAMP_DEBUG
		printf("[WiFi] Reconnection failed, will retry in next cycle\n");
		delay(10);
		#endif /* VAMP_DEBUG */
		delay(RECONNECT_DELAY);
		return false;
	}
}

/** Configuración de IP estática para la STA WiFi */
void esp8266_sta_static_ip(IPAddress ip, IPAddress gateway, IPAddress subnet, IPAddress dns1, IPAddress dns2) {
	WiFi.config(ip, gateway, subnet, dns1, dns2);
}


/** Inicializar STA WiFi con SSID y password */
bool esp8266_sta_init(const char * wifi_ssid, const char * wifi_password){

	/* Guardar credenciales WiFi en variables locales */
	wifi_ssid_local = String(wifi_ssid);
	wifi_password_local = String(wifi_password);
	return esp8266_conn();

}

/* Función unificada para enviar datos por HTTP/HTTPS */
size_t esp8266_http_request(const vamp_profile_t * profile, char * data, size_t data_size) {
	if (!esp8266_check_conn()) {
		#ifdef VAMP_DEBUG
		printf("[WiFi] Not connected\n");
		delay(10);
		#endif /* VAMP_DEBUG */
		return 0;
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
	
	#ifdef VAMP_DEBUG
	printf("[WiFi] Connecting to: %s, with  %d query params and %d protocol options\n", 
				full_url.c_str(), 
				profile->query_params.count, 
				profile->protocol_options.count);
	#endif /* VAMP_DEBUG */

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
	if( profile->protocol_options.count > 0 ) {
		for (uint8_t i = 0; i < profile->protocol_options.count; i++) {
			const char* key = profile->protocol_options.pairs[i].key;
			const char* value = profile->protocol_options.pairs[i].value;
			
			if (key[0] != '\0' && value[0] != '\0') {
				https_http.addHeader(key, value);
				
				#ifdef VAMP_DEBUG
				printf("[WiFi] Adding header: %s = %s\n", key, value);
				delay(10);
				#endif /* VAMP_DEBUG */
			}
		}
	}

	/* Enviar request según método */
	int httpResponseCode = -1;
	
	switch (profile->method) {
		/* GET */
		case VAMP_HTTP_METHOD_GET:
			#ifdef VAMP_DEBUG
			printf("[WiFi] Sending GET request %s...\n", is_https ? "(HTTPS)" : "(HTTP)");
			delay(10);
			#endif /* VAMP_DEBUG */
			httpResponseCode = https_http.GET();
			break;
		case VAMP_HTTP_METHOD_POST:
			#ifdef VAMP_DEBUG
			printf("[WiFi] Sending POST request %s...\n", is_https ? "(HTTPS)" : "(HTTP)");
			delay(10);
			#endif /* VAMP_DEBUG */
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
			printf("[WiFi] Response length invalid: %d\n", response.length());
			delay(10);
			#endif /* VAMP_DEBUG */
			return 0;
		}
		
		/* Verificar que el buffer proporcionado sea suficiente */
		if (VAMP_IFACE_BUFF_SIZE < response.length()) {
			#ifdef VAMP_DEBUG
			printf("[WiFi] Insufficient buffer size: %d bytes required\n", response.length());
			delay(10);
			#endif /* VAMP_DEBUG */
			return 0;

		}
		/* Copiar respuesta al buffer proporcionado */
		sprintf(data, "%s", response.c_str());

		#ifdef VAMP_DEBUG
		printf("[WiFi] response %d bytes, code: %d\n", response.length(), httpResponseCode);
		delay(10);
		#endif /* VAMP_DEBUG */
		https_http.end();

		return strlen(data); // Éxito

	} else {
		#ifdef VAMP_DEBUG
		Serial.print("[WiFi] Error in ");
		Serial.print(is_https ? "HTTPS" : "HTTP");
		Serial.print(": ");
		Serial.println(httpResponseCode);
		#endif /* VAMP_DEBUG */
		https_http.end(); // Cerrar conexión solo en caso de error
		return 0;
	}
}

#endif // ARDUINO_ARCH_ESP8266

