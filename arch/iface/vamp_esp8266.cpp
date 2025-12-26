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

/* Tamanos seguros */

/* El URL tiene un tamaño máximo que incluye el endpoint y los parámetros */
 #define VAMP_URL_MAX_LEN (VAMP_ENDPOINT_MAX_LEN + (VAMP_KEY_MAX_LEN + VAMP_VALUE_MAX_LEN) * 4 + 1)
 static char full_url[VAMP_URL_MAX_LEN];

/* Timeout para conexión WiFi (segundos) */
#define WIFI_TIMEOUT 		30
#define RECONNECT_DELAY 	1000              // Delay para reconexión WiFi (ms)

// Configuración HTTPS para comunicación con VAMP Registry y endpoints
#define HTTPS_TIMEOUT 		8000              // Timeout para requests HTTPS (ms)
#define HTTPS_USER_AGENT 	"VAMP-Gateway/1.0" // User agent para requests

static char * wifi_ssid_local = NULL;
static char * wifi_password_local = NULL;

/* -----------------------------  /WiFi --------------------------------- */


/* Objetos globales para comunicación (reutilizables - estáticos para persistencia) */
static WiFiClientSecure tcp_secure_client;
static WiFiClient tcp_client;
static HTTPClient https_http;

/* Inicializar cliente HTTPS */
bool esp8266_conn(void){

	/*  WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 */
	WiFi.mode(WIFI_STA);
	WiFi.begin(wifi_ssid_local, wifi_password_local);
	
	// Esperar conexión (timeout de 30 segundos)
	int timeout = WIFI_TIMEOUT;

	#ifdef VAMP_DEBUG
	printf("[WiFi] Connecting to SSID: %s\n [WiFi] ", wifi_ssid_local);
	#endif /* VAMP_DEBUG */

	while (WiFi.status() != WL_CONNECTED && timeout > 0) {
		delay(1000);
		
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

	#ifdef VAMP_DEBUG
	printf("\n");
	#endif
	
	if (WiFi.status() == WL_CONNECTED) {

		#ifdef VAMP_DEBUG
		printf("\n[WiFi] connected! IP: %s\n", WiFi.localIP().toString().c_str());
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
		#endif /* VAMP_DEBUG */
		return true;
	} else {
		#ifdef VAMP_DEBUG
		printf("[WiFi] Reconnection failed, will retry in next cycle\n");
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

	/* Validar wifi_ssid y wifi_password */
	if (wifi_ssid == NULL || wifi_password == NULL ||
			strnlen(wifi_ssid, VAMP_GW_NAME_MAX_LEN) == 0 || 
			strnlen(wifi_password, VAMP_GW_NAME_MAX_LEN) == 0 ||
			strnlen(wifi_ssid, VAMP_SSID_MAX_LEN) >= VAMP_SSID_MAX_LEN ||
			strnlen(wifi_password, VAMP_PASSWORD_MAX_LEN) >= VAMP_PASSWORD_MAX_LEN) {
		#ifdef VAMP_DEBUG
		printf("[WiFi] Invalid SSID or password\n");
		#endif /* VAMP_DEBUG */
		return false;
	}

	/* Crear memoria para credenciales WiFi en variables locales */
	if (wifi_ssid_local != NULL) {
		free(wifi_ssid_local);
	}
	if (wifi_password_local != NULL) {
		free(wifi_password_local);
	}

	wifi_ssid_local = (char*)malloc(strnlen(wifi_ssid, VAMP_SSID_MAX_LEN) + 1);
	if (wifi_ssid_local) {
		strncpy(wifi_ssid_local, wifi_ssid, VAMP_SSID_MAX_LEN);
	}
	wifi_password_local = (char*)malloc(strnlen(wifi_password, VAMP_PASSWORD_MAX_LEN) + 1);
	if (wifi_password_local) {
		strncpy(wifi_password_local, wifi_password, VAMP_PASSWORD_MAX_LEN);
	}

	return esp8266_conn();

}

/** Inicializar clientes TCP */
void esp8266_tcp_init(void) {
	/* Inicializar cliente TCP seguro (HTTPS) */
	tcp_secure_client.setInsecure(); // ToDo !!! solo para desarrollo
	//tcp_secure_client.setBufferSizes(1024, 1024);
	//tcp_secure_client.setTimeout(6000); // Timeout reducido: 3 segundos

	/* Inicializar cliente TCP normal (HTTP) */
	//tcp_client.setBufferSizes(1024, 1024);
	//tcp_client.setTimeout(6000); // Timeout reducido: 3 segundos

	#ifdef VAMP_DEBUG
	printf("[TCP] Clients ok\n");
	#endif /* VAMP_DEBUG */
}

/* Función unificada para enviar datos por HTTP/HTTPS */
/* ToDo aqui data_size que medio como a la bartola hay que ver que bola con esto */
size_t esp8266_http_request(const vamp_profile_t * profile, char * data, size_t data_size) {

	/* Verificar conexión WiFi */
	if (!esp8266_check_conn()) {
		#ifdef VAMP_DEBUG
		printf("[WiFi] Not connected\n");
		#endif /* VAMP_DEBUG */
		return 0;
	}

	/* Cheque de todas las variables de entrada */
	if (profile == NULL || data == NULL || data_size == 0) {
		#ifdef VAMP_DEBUG
		printf("[HTTP] Invalid request parameters\n");
		#endif /* VAMP_DEBUG */
		return 0;
	}

	size_t full_url_len = strnlen(profile->endpoint_resource, VAMP_ENDPOINT_MAX_LEN);

	/* Chequeo del recurso */
	if (profile->endpoint_resource == NULL || 
			full_url_len == 0 || 
			full_url_len >= VAMP_ENDPOINT_MAX_LEN) {
		/* Si llega aqui es que es un cadena vacía o no hay un '\0' dentro del límite */
		#ifdef VAMP_DEBUG
		printf("[HTTP] Invalid endpoint resource\n");
		#endif /* VAMP_DEBUG */
		return 0;
	}

	/* Chequeo del protocolo */
	uint8_t profile_protocol = 0;
	/* https */
	if(strncmp(profile->endpoint_resource, "https://", 8) == 0) {
		profile_protocol = VAMP_PROTOCOL_HTTPS;
	}
	/* http */ 
	else if (strncmp(profile->endpoint_resource, "http://", 7) == 0) {
		profile_protocol = VAMP_PROTOCOL_HTTP;
	}
	/* ... resto de los protocolos no implementados */
	/* Protocolo no soportado */ 
	else {
		#ifdef VAMP_DEBUG
		printf("[HTTP] Unsupported protocol in endpoint resource\n");
		#endif /* VAMP_DEBUG */
		return 0;
	}
	
	/* Chequeo del método relativo al protocolo */
	switch (profile_protocol) {
		case VAMP_PROTOCOL_HTTP:
			/* Métodos HTTP soportados: GET y POST */
			if (profile->method != VAMP_HTTP_METHOD_GET && profile->method != VAMP_HTTP_METHOD_POST) {
				#ifdef VAMP_DEBUG
				printf("[HTTP] Unsupported HTTP method: %d\n", profile->method);
				#endif /* VAMP_DEBUG */
				return 0;
			}
			break;
		case VAMP_PROTOCOL_HTTPS:
			/* Métodos HTTPS soportados: GET y POST */
			if (profile->method != VAMP_HTTP_METHOD_GET && profile->method != VAMP_HTTP_METHOD_POST) {
				#ifdef VAMP_DEBUG
				printf("[HTTP] Unsupported HTTPS method: %d\n", profile->method);
				#endif /* VAMP_DEBUG */
				return 0;
			}
			break;
		/* ToDo Aqui faltaria evaluar los otros protocolos cuando se implementen */
		default:
			break;
	}
	
	/* Construir URL completa */
	sprintf(full_url, "%s", profile->endpoint_resource);

	/* Añadir parámetros de consulta si existen */
	if (profile->query_params.count > 0 && profile->query_params.pairs != NULL) {
		
		char query_buffer[(VAMP_KEY_MAX_LEN + VAMP_VALUE_MAX_LEN) * 4 + 1];
		
		/* Convertir query_params a query string */
		size_t len = vamp_kv_to_query_string(&profile->query_params, query_buffer, sizeof(query_buffer));
		/* y adicionarlo al url */
		if (len > 0) {
			sprintf(full_url + strnlen(full_url, VAMP_URL_MAX_LEN), "?%s", query_buffer);
		}
	}
	
	#ifdef VAMP_DEBUG
	printf("[HTTP] Remote: %s\n [HTTP] query params: %d - protocol optionsand: %d\n", 
				full_url, 
				profile->query_params.count, 
				profile->protocol_options.count);
	#endif /* VAMP_DEBUG */

	/* Hacer siempre una nueva conexion */
	https_http.setReuse(false);

	/* Discriminar entre HTTP y HTTPS */
	if (profile_protocol == VAMP_PROTOCOL_HTTPS) {
		/* Configurar cliente HTTPS (usando variable estática con persistencia) */
		#ifdef VAMP_DEBUG
		printf("[HTTP] secured\n");
		#endif /* VAMP_DEBUG */
		https_http.begin(tcp_secure_client, full_url);
	} 
	else if (profile_protocol == VAMP_PROTOCOL_HTTP) {
		/* Configurar cliente HTTP normal (usando variable estática con persistencia) */
		#ifdef VAMP_DEBUG
		printf("[HTTP] plain\n");
		#endif /* VAMP_DEBUG */
		https_http.begin(tcp_client, full_url);
	}
	/* rest of the protocols not implemented */

	/* Configurar User-Agent */
	/* ToDo: Pasar el ID del gateway como parte del User-Agent para que el extremo pueda identificarlo
	y verificar la autenticidad de la solicitud */
	https_http.setUserAgent(HTTPS_USER_AGENT);

	/* Añadir headers personalizados desde key-value store */
	if( profile->protocol_options.count > 0 && profile->protocol_options.pairs != NULL ) {
		for (uint8_t i = 0; i < profile->protocol_options.count; i++) {

			/* Verificar entradas nulas */
			if(profile->protocol_options.pairs[i].key == NULL || profile->protocol_options.pairs[i].value == NULL) {
				continue; 
			}

			const char* key = profile->protocol_options.pairs[i].key;
			const char* value = profile->protocol_options.pairs[i].value;
			
			if (key[0] != '\0' && value[0] != '\0') {
				https_http.addHeader(key, value);
				
				#ifdef VAMP_DEBUG
				printf("[HTTP] Adding header: %s = %s\n", key, value);
				#endif /* VAMP_DEBUG */
			}
		}
	}

	/* Enviar request según método */
	int httpResponseCode = -1;
	
	Serial.printf("{MEN} free heap: %d B\n", ESP.getFreeHeap());
    Serial.printf("{MEN} frag. Heap: %d B\n", ESP.getHeapFragmentation());
    Serial.printf("{MEN} max free block: %d B\n", ESP.getMaxFreeBlockSize());

	switch (profile->method) {
		/* GET */
		case VAMP_HTTP_METHOD_GET:
			#ifdef VAMP_DEBUG
			printf("[WiFi] Sending GET request %s...\n", (profile_protocol == VAMP_PROTOCOL_HTTPS) ? "(HTTPS)" : "(HTTP)");
			#endif /* VAMP_DEBUG */
			httpResponseCode = https_http.GET();
			break;
		/* POST */
		case VAMP_HTTP_METHOD_POST:
			#ifdef VAMP_DEBUG
			printf("[WiFi] Sending POST request %s...\n", (profile_protocol == VAMP_PROTOCOL_HTTPS) ? "(HTTPS)" : "(HTTP)");
			#endif /* VAMP_DEBUG */
			httpResponseCode = https_http.POST((uint8_t*)data, data_size);
			break;
		default:
			break;
	}
	
	// Procesar respuesta
	if (httpResponseCode > 0) {

		/* Obtener respuesta como String */
		String response = https_http.getString();
		/* Cerrar conexión */
		https_http.end();

		#ifdef VAMP_DEBUG
		printf("[WiFi] Response code: %d - ", httpResponseCode);
		#endif /* VAMP_DEBUG */

		/* Aqui se cheque el codigo de respuesta... */
		if(httpResponseCode != HTTP_CODE_OK) {

			#ifdef VAMP_DEBUG
			printf("fail\n");
			#endif /* VAMP_DEBUG */

			return 0;
		}

		if(response.length() == 0 || response.length() >= data_size) {
			/* Buffer nulo o demasiado grande */
			#ifdef VAMP_DEBUG
			printf("resplength invalid: %d\n", response.length());
			#endif /* VAMP_DEBUG */
			return 0;
		}
		
		/* Copiar respuesta al buffer proporcionado de forma segura */
		sprintf(data, "%s", response.c_str());

		#ifdef VAMP_DEBUG
		printf("data: %s\n", data);
		#endif /* VAMP_DEBUG */

		return response.length(); // Éxito

	} else {
		#ifdef VAMP_DEBUG
		printf("[WiFi] Error in %s: %d\n", ((profile_protocol == VAMP_PROTOCOL_HTTPS) ? "HTTPS" : "HTTP"), httpResponseCode);
		#endif /* VAMP_DEBUG */
		https_http.end();

		/* Si es -1 es que no hay conexión asi que se puede intentar reconectar o algo asi */ 

		return 0;
	}
}

#endif // ARDUINO_ARCH_ESP8266

