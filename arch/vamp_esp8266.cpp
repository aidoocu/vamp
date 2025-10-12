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

#include "../../hmi/display.h"

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


#ifdef OLED_DISPLAY
/* Dibuja barra doble de nivel de señal WiFi en el extremo izquierdo del display (x=0 y x=2)
Altura máxima: 48 px (y=0 a y=47), 5 niveles de abajo hacia arriba */
void draw_wifi_signal_bar(void) {
	
	int rssi = WiFi.RSSI();

	#ifdef VAMP_DEBUG
	Serial.print("WiFi RSSI: ");
	Serial.println(rssi);
	#endif /* VAMP_DEBUG */

	/* Mapear RSSI a nivel (0-4) */
	int level = 0;
	if (rssi >= -67) level = 4;         // Excelente
	else if (rssi >= -70) level = 3;    // Buena
	else if (rssi >= -80) level = 2;    // Aceptable
	else if (rssi >= -90) level = 1;    // Débil
	else level = 0;                     // Muy débil

	/* Limpiar barra previa */
	display.fillRect(0, 0, 3, 48, SSD1306_BLACK);

	/* Cada nivel ocupa 48/5 = 9.6 px, redondeamos a 9 px por nivel */
	for (int i = 0; i <= level; ++i) {
		int y0 = 47 - i * 9;
		int y1 = y0 - 8;
		if (y1 < 0) y1 = 0;
		// Dibuja dos líneas verticales separadas por 1 px
		display.drawLine(0, y1, 0, y0, SSD1306_WHITE);
		display.drawLine(2, y1, 2, y0, SSD1306_WHITE);
	}

	/* Actualizar display */
	display.display();
}
#endif


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
	while (WiFi.status() != WL_CONNECTED && timeout > 0) {
		delay(1000);

		#ifdef VAMP_DEBUG
		Serial.print(".");
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
			Serial.print("w_on");
			#endif /* VAMP_DEBUG */
		} else {
			display.fillRect(5, 48, 16, 16, SSD1306_BLACK);
			display.drawBitmap(5, 48, wifi_off_icon_16x16, 16, 16, SSD1306_WHITE);
			display.display();

			#ifdef VAMP_DEBUG
			Serial.print("w_off");
			#endif /* VAMP_DEBUG */
		}
		#endif	 /* OLED_DISPLAY */

		#ifdef VAMP_DEBUG
		
		#endif

		timeout--;
	
	}
	
	if (WiFi.status() == WL_CONNECTED) {

		#ifdef VAMP_DEBUG
		Serial.println();
		Serial.print("WiFi conectado! IP: ");
		Serial.println(WiFi.localIP());
		Serial.println("WiFi inicializado correctamente");
		#endif /* VAMP_DEBUG */

		/* Display */
		#ifdef OLED_DISPLAY
		display.fillRect(5, 48, 16, 16, SSD1306_BLACK);
		display.drawBitmap(5, 48, wifi_icon_16x16, 16, 16, SSD1306_WHITE);
		display.display();
		draw_wifi_signal_bar();
		#endif	 /* OLED_DISPLAY */

		return true;

	} else {

		#ifdef VAMP_DEBUG
		Serial.println();
		Serial.println("Error: No se pudo conectar a WiFi");
		Serial.println("Error al inicializar WiFi");
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

		#ifdef OLED_DISPLAY
		draw_wifi_signal_bar();
		#endif /* OLED_DISPLAY */

		return true;
	}

	#ifdef VAMP_DEBUG
	Serial.println("Conexión WiFi perdida, intentando reconectar...");
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
		Serial.println("Error: WiFi no conectado");
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
			return 0;
		}
		
		/* Verificar que el buffer proporcionado sea suficiente */
		if (VAMP_IFACE_BUFF_SIZE < response.length()) {
			#ifdef VAMP_DEBUG
			Serial.println("Error: Buffer insuficiente");
			#endif /* VAMP_DEBUG */
			return 0;

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

		return strlen(data); // Éxito

	} else {
		#ifdef VAMP_DEBUG
		Serial.print("Error en ");
		Serial.print(is_https ? "HTTPS" : "HTTP");
		Serial.print(": ");
		Serial.println(httpResponseCode);
		#endif /* VAMP_DEBUG */
		https_http.end(); // Cerrar conexión solo en caso de error
		return 0;
	}
}

#endif // ARDUINO_ARCH_ESP8266

