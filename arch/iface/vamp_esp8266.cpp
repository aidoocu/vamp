/** 
 * 
 * 
 * 
 * 
 * 
 */

 

/* -----------------------------  ESP8266 --------------------------------- */

#if defined(ARDUINO_ARCH_ESP8266)

#include "vamp_esp8266.h"

/* ToDo esto asi esta feo */
#include "../../vamp_gw.h"
#include "../../vamp_client.h"
#include "../../vamp_callbacks.h"

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

//static char * wifi_ssid_local = NULL;
//static char * wifi_password_local = NULL;

static gw_config_t * vamp_conf_local = NULL;

/* -----------------------------  /WiFi --------------------------------- */


/* Objetos globales para comunicación (reutilizables - estáticos para persistencia) */
static WiFiClientSecure tcp_secure_client;
static bool tls_ok = false;
static WiFiClient tcp_client;
static HTTPClient https_http;

/* Intentar reservar memoria TLS haciendo un handshake de prueba */
static bool esp8266_tls_init() {
	/* Verificar si hay suficiente memoria para el handshake
	6K para TLS + 2K para overhead + 512 + 256 para buffers */
	if (ESP.getFreeHeap() < (6000 + 2048 + 512 + 256)) {
		#ifdef VAMP_DEBUG
		printf("[TLS] Not enough heap for TLS init\n");
		#endif
		return false;
	}

	#ifdef VAMP_DEBUG
	printf("[TLS] Attempting handshake to reserve memory...\n");
	#endif

	tcp_secure_client.setInsecure();
	/* utilizar tamaños de buffers mínimos para garantizar la conexión */
	tcp_secure_client.setBufferSizes(512, 256);

	/* Handshake de prueba para reservar memoria TLS */
	bool handshake_ok = tcp_secure_client.connect("httpbin.org", 443);
	/* E inmediatamente pase lo que pase cerrar la conexión */
	tcp_secure_client.stop();

	/* Evaluar resultado del handshake */
	if(handshake_ok) {
		#ifdef VAMP_DEBUG
		printf("[TLS] Handshake OK\n");
		#endif
		return true;
	} else {
		#ifdef VAMP_DEBUG
		printf("[TLS] Handshake failed\n");
		#endif
		return false;
	}
}

/* Inicializar cliente HTTPS */
bool esp8266_conn() {

	/* Si ya estamos conectados, no reinicializar */
	if (WiFi.status() == WL_CONNECTED) {
		#ifdef VAMP_DEBUG
		char ip_buf[16];
		IPAddress ip = WiFi.localIP();
		snprintf(ip_buf, sizeof(ip_buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
		printf("[WiFi] Already connected to %s, IP: %s\n", 
			vamp_conf_local->wifi.ssid, ip_buf);
		#endif
		return true;
	}

	/*  WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 */
	WiFi.mode(WIFI_STA);
	
	/* Si debe ser configurada como estática */
	if (vamp_conf_local->net.mode && vamp_conf_local->net.mode == VAMP_NET_STATIC) {

		/* Validar que la IP estática esté definida (no 0.0.0.0) */
		IPAddress ip = vamp_conf_local->net.ip;
		if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
			#ifdef VAMP_DEBUG
			printf("[NET] Error: NET.mode == static pero NET.ip no está definida\n");
			#endif
			
			/* No tiene sentido continuar sin IP */
			return false;
		}

		/* Preparar valores de red, usar defaults cuando falten */
			IPAddress gateway = vamp_conf_local->net.gateway;
			IPAddress subnet = vamp_conf_local->net.subnet;
			IPAddress dns1 = vamp_conf_local->net.dns1;
			IPAddress dns2 = vamp_conf_local->net.dns2;

		// Subnet por defecto
		if (subnet[0] == 0 && subnet[1] == 0 && subnet[2] == 0 && subnet[3] == 0) {
			subnet = IPAddress(255,255,255,0);
			#ifdef VAMP_DEBUG
			printf("[NET] Subnet no proporcionada, usando 255.255.255.0\n");
			#endif
		}

		// Gateway por defecto: misma red que la IP con .1
		if (gateway[0] == 0 && gateway[1] == 0 && gateway[2] == 0 && gateway[3] == 0) {
			gateway = ip;
			gateway[3] = 1;
			#ifdef VAMP_DEBUG
			printf("[NET] Gateway no proporcionado, usando: %d.%d.%d.%d\n", 
			       gateway[0], gateway[1], gateway[2], gateway[3]);
			#endif
		}

		// DNS por defecto
		if (dns1[0] == 0 && dns1[1] == 0 && dns1[2] == 0 && dns1[3] == 0) {
			dns1 = gateway;
			#ifdef VAMP_DEBUG
			printf("[NET] DNS1 no proporcionado, usando gateway: %d.%d.%d.%d\n", 
			       dns1[0], dns1[1], dns1[2], dns1[3]);
			#endif
		}
		if (dns2[0] == 0 && dns2[1] == 0 && dns2[2] == 0 && dns2[3] == 0) {
			dns2 = IPAddress(8,8,8,8);
			#ifdef VAMP_DEBUG
			printf("[NET] DNS2 no proporcionado, usando 8.8.8.8\n");
			#endif
		}

		WiFi.config(ip, gateway, subnet, dns1, dns2);
	}

	WiFi.begin(vamp_conf_local->wifi.ssid, vamp_conf_local->wifi.password);
	
	// Esperar conexión (timeout de 30 segundos)
	int timeout = WIFI_TIMEOUT;

	#ifdef VAMP_DEBUG
	printf("[WiFi] Connecting to SSID: %s\n [WiFi] ", vamp_conf_local->wifi.ssid);
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
		char ip_buf[16];
		IPAddress ip = WiFi.localIP();
		snprintf(ip_buf, sizeof(ip_buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
		printf("\n[WiFi] connected! IP: %s\n", ip_buf);
		#endif /* VAMP_DEBUG */

		/* Display */
		#ifdef OLED_DISPLAY
		display.fillRect(5, 48, 16, 16, SSD1306_BLACK);
		display.drawBitmap(5, 48, wifi_icon_16x16, 16, 16, SSD1306_WHITE);
		display.display();
		//draw_wifi_signal_bar();
		#endif	 /* OLED_DISPLAY */

		/* Inicialización de la conexion TLS, aqui se reserva heap para BearSSL */
		tls_ok = esp8266_tls_init();

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
	printf("[WiFi] Lost connection, attempting to reconnect...\n");
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
	/* ToDo a oartir de aqui da un palo y se reinicia, al menos me ha pasado cuando 
	las credenciales de la wifi no son correctas y no se puede cnectar, asu que seguir
	testinando y resolver */
	esp8266_conn();
	
	if (WiFi.status() == WL_CONNECTED) {
		#ifdef VAMP_DEBUG
		char ip_buf[16];
		IPAddress ip = WiFi.localIP();
		snprintf(ip_buf, sizeof(ip_buf), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
		printf("[WiFi] Reconnected! IP: %s\n", ip_buf);
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

/** Inicializar STA WiFi con SSID y password */
bool esp8266_sta_init(const gw_config_t * config){

	/* Validar wifi_ssid y wifi_password */
	if (config->wifi.ssid == NULL || config->wifi.password == NULL ||
			strnlen(config->wifi.ssid, VAMP_GW_NAME_MAX_LEN) == 0 || 
			strnlen(config->wifi.password, VAMP_GW_NAME_MAX_LEN) == 0 ||
			strnlen(config->wifi.ssid, VAMP_SSID_MAX_LEN) >= VAMP_SSID_MAX_LEN ||
			strnlen(config->wifi.password, VAMP_PASSWORD_MAX_LEN) >= VAMP_PASSWORD_MAX_LEN) {
		#ifdef VAMP_DEBUG
		printf("[WiFi] Invalid SSID or password\n");
		#endif /* VAMP_DEBUG */
		return false;
	}



	/* Guardamos el puntero al config local */
	vamp_conf_local = (gw_config_t *)config;

	/* Pasar config al esp8266_conn para configuración de red estática */
	return esp8266_conn();

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

	/* Chequeo del recurso */
	if (profile->endpoint_resource == NULL) {
		#ifdef VAMP_DEBUG
		printf("[HTTP] Invalid endpoint resource (NULL)\n");
		#endif /* VAMP_DEBUG */
		return 0;
	}

	size_t full_url_len = strnlen(profile->endpoint_resource, VAMP_ENDPOINT_MAX_LEN);
	if (full_url_len == 0 || full_url_len >= VAMP_ENDPOINT_MAX_LEN) {
		/* Si llega aqui es que es un cadena vacía o no hay un '\0' dentro del límite */
		#ifdef VAMP_DEBUG
		printf("[HTTP] Invalid endpoint resource length\n");
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

	//sprintf(full_url,"%s", "https://httpbin.org/get"); // ToDo eliminar esto
	//profile_protocol = VAMP_PROTOCOL_HTTPS;      // ToDo eliminar esto
	
	#ifdef VAMP_DEBUG
	printf("[HTTP] Remote: %s\n[HTTP] query params: %d - protocol options: %d\n", 
				full_url, 
				profile->query_params.count, 
				profile->protocol_options.count);
	#endif /* VAMP_DEBUG */

	/* Hacer siempre una nueva conexion */
	https_http.setReuse(false);

	/* Discriminar entre HTTP y HTTPS */
	if (profile_protocol == VAMP_PROTOCOL_HTTPS) {

		/* verificar si TLS está disponible, si no, intentar inicializarlo */
		if (!tls_ok) {
			#ifdef VAMP_DEBUG
			printf("[HTTP] TLS not initialized, attempting to initialize...\n");
			#endif /* VAMP_DEBUG */
			tls_ok = esp8266_tls_init();
			if (!tls_ok) {
				#ifdef VAMP_DEBUG
				printf("[HTTP] TLS initialization failed, cannot proceed with HTTPS request\n");
				#endif /* VAMP_DEBUG */
				return 0;
			}
		}

		/* verificar cuanta memoria libre hay para TLS */
		if (ESP.getFreeHeap() < MIN_HEAP_FOR_TLS) {
			#ifdef VAMP_DEBUG
			printf("[HTTP] Not enough heap for TLS\n");
			#endif /* VAMP_DEBUG */
			return 0;
		}

		/* ToDo data_size tiene el tamano del buffer asi que el buffer para TLS puede ser ajustado en consecuencia 
		evitando reservar mas de lo necesario. hay que verificar que pasa cuando el servidor envia un texto mas largo 
		que el buffer ???? */

		tcp_secure_client.setBufferSizes(TLS_BUFFER_SIZE_RX, TLS_BUFFER_SIZE_TX);
		
		/* Configurar cliente HTTPS justo antes de usarlo */
		tcp_secure_client.setInsecure(); // ToDo: usar certificados en producción
		
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

	https_http.setTimeout(HTTPS_TIMEOUT);

	/* Enviar request según método */
	int httpResponseCode = -1;
	
	switch (profile->method) {
		/* GET */
		case VAMP_HTTP_METHOD_GET:
			#ifdef VAMP_DEBUG
			printf("[HTTP] Sending GET request %s...\n", (profile_protocol == VAMP_PROTOCOL_HTTPS) ? "(HTTPS)" : "(HTTP)");
			#endif /* VAMP_DEBUG */
			httpResponseCode = https_http.GET();
			break;
		/* POST */
		case VAMP_HTTP_METHOD_POST:
			#ifdef VAMP_DEBUG
			printf("[HTTP] Sending POST request %s...\n", (profile_protocol == VAMP_PROTOCOL_HTTPS) ? "(HTTPS)" : "(HTTP)");
			#endif /* VAMP_DEBUG */
			httpResponseCode = https_http.POST((uint8_t*)data, data_size);
			break;
		default:
			break;
	}
	
	/* ------------------- Procesar respuesta  ------------------- */
	
	bool fail = false;
	int total_read = 0;
	
	if (httpResponseCode > 0) {

		#ifdef VAMP_DEBUG
		printf("[HTTP] Response code: %d - ", httpResponseCode);
		#endif /* VAMP_DEBUG */

		/* Aqui se cheque el codigo de respuesta... */
		if(httpResponseCode != HTTP_CODE_OK) {

			#ifdef VAMP_DEBUG
			printf("fail\n");
			#endif /* VAMP_DEBUG */

			fail = true;
			goto end_response;
		}

		WiFiClient * stream = https_http.getStreamPtr();
		
		if (!stream) {
			#ifdef VAMP_DEBUG
			printf("[HTTP] No stream available\n");
			#endif
			fail = true;
			goto end_response;
		}

		/* Determinar si es chunked o tiene Content-Length */
		int content_len = https_http.getSize();
		bool is_chunked = (content_len < 0);

		total_read = 0;

		if (is_chunked) {
		    /* CHUNKED: Decodificar manualmente SIN string */
		    while (total_read < (int)(data_size - 1)) {
		        /* Leer tamaño del chunk (línea HEX) byte a byte */
		        char hex_buf[16];
		        int hex_idx = 0;
		        
		        while (hex_idx < 15 && stream->available()) {
		            char c = stream->read();
		            if (c == '\r' || c == '\n') break;
		            hex_buf[hex_idx++] = c;
		        }
		        hex_buf[hex_idx] = '\0';
		        
		        /* Convertir HEX a decimal */
		        int chunk_size = (int)strtol(hex_buf, NULL, 16);
		        
		        /* Chunk de tamaño 0 = fin */
		        if (chunk_size == 0) break;
		        
		        /* Saltar \n si no se leyó con \r */
		        if (stream->peek() == '\n') stream->read();
		        
		        /* Verificar que no exceda buffer */
		        if (total_read + chunk_size >= (int)(data_size - 1)) {
		            chunk_size = (data_size - 1) - total_read;
		        }
		        
		        /* Leer chunk directamente en data */
		        int read = stream->readBytes((uint8_t*)(data + total_read), chunk_size);
		        if (read <= 0) break;
		        
		        total_read += read;
		        
		        /* Saltar \r\n final del chunk */
		        while (stream->available() && (stream->peek() == '\r' || stream->peek() == '\n')) {
		            stream->read();
		        }
		        
		        /* Si buffer lleno, salir */
		        if (total_read >= (int)(data_size - 1)) break;
		    }
		} else {
		    /* CONTENT-LENGTH: Leer directamente */
		    if (content_len >= (int)data_size) {
		        content_len = data_size - 1;
		    }
		    
		    total_read = stream->readBytes((uint8_t * )data, content_len);
		}

		if (total_read <= 0) {
			#ifdef VAMP_DEBUG
			printf("[HTTP] Empty response\n");
			#endif
			fail = true;
			goto end_response;
		}

		data[total_read] = '\0';

		#ifdef VAMP_DEBUG
		printf("data: %s\n", data);
		#endif /* VAMP_DEBUG */
		
		/* Éxito */
		goto end_response; 

	} else {
		#ifdef VAMP_DEBUG
		printf("[WiFi] Error in %s: %d\n", ((profile_protocol == VAMP_PROTOCOL_HTTPS) ? "HTTPS" : "HTTP"), 
													httpResponseCode);
		#endif /* VAMP_DEBUG */
		https_http.end();
		
		/* Liberar recursos del cliente TLS en caso de error */
		if (profile_protocol == VAMP_PROTOCOL_HTTPS) {
			tcp_secure_client.stop();
		}

		/* Si es -1 es que no hay conexión asi que se puede intentar reconectar o algo asi */ 
	}

	end_response:

	/* Cerrar la conexión */
	https_http.end();

	/* Liberar recursos del cliente TLS en caso de error */
	switch (profile_protocol) {
		case VAMP_PROTOCOL_HTTPS:
			tcp_secure_client.stop();
			break;
		case VAMP_PROTOCOL_HTTP:
			/* No hay recursos especiales que liberar */
			tcp_client.stop();
			break;
		/* ToDo Aqui faltaria evaluar los otros protocolos cuando se implementen */
		default:
			break;
	}
	
	if (fail) {
		return 0;
	}

			
	return (size_t)total_read;
}

#endif // ARDUINO_ARCH_ESP8266

