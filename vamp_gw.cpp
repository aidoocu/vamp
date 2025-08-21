/** @brief VAMP Gateway
 * 
 * 
 * 
 * 
 */
#include "vamp_gw.h"
#include "vamp_client.h"
#include "vamp_callbacks.h"
#include <ArduinoJson.h>
#ifdef ARDUINO_ARCH_ESP8266
#include <cstring>
#include <cstdlib>
#endif

// Tabla unificada VAMP (NAT + Device + Session)
static vamp_entry_t vamp_table[VAMP_MAX_DEVICES];

/* Profile del recurso VREG */
static vamp_profile_t vamp_vreg_profile;

/* Fecha de la última actualización de la tabla en UTC */
static char last_table_update[] = VAMP_TABLE_INIT_TSMP;

/* ID del gateway */
static char vamp_gw_id[VAMP_GW_ID_MAX_LEN];


/* Buffer para la solicitud y respuesta de internet */
static char req_resp_internet_buff[VAMP_IFACE_BUFF_SIZE];

// Contadores globales
//static uint8_t vamp_device_count = 0;

/* Declaracion de funciones locales  */

/** @brief respuesta de sincronización
 * El buffer contiene la respuesta que debe tener el formato:
 * gateway_sync_resp <--option> <value>
 * <value> es el nuevo valor para la opción
 * Opciones:
 * - "--updated": indica que la tabla ya está actualizada, no hay nuevos datos, no tiene value
 * - "--error": indica que hubo un error en la sincronización, value contiene el mensaje de error
 * - "--node": contiene los datos de la tabla en formato JSON, value es el JSON con los campos:
 *  	rf_id, action, type, resource
 * actions:
 * - "ADD": agregar un dispositivo. El VREG no sabe ni debe intervenir en el puerto, 
 *          así que el gateway busca un slot vacío y asigna el nuevo nodo. Se revisa 
 *          primero si el nodo ya estaba en la tabla.
 * - "REMOVE": eliminar un dispositivo, se cambia el estado a libre y se pone a cero el rf_id
 * - "UPDATE": actualizar un dispositivo existente
 * @param json_data: puntero a los datos JSON de la respuesta
 * @return true si la respuesta es válida, false en caso contrario
 */
bool vamp_process_sync_json_response(const char* json_data);


/** @brief Funciones que manejan la tabla unificada VAMP
 * 
 * @param index Table index 0 <= index < VAMP_MAX_DEVICES
 * @param rf_id WSN ID (have VAMP_ADDR_LEN bytes)
 * @return true if the operation was successful, false otherwise
 */
bool vamp_remove_device(const uint8_t* rf_id);
void vamp_clear_entry(int index);

/** @return The index of the device if found, VAMP_MAX_DEVICES not 
 *  found otherwise */
uint8_t vamp_find_device(const uint8_t* rf_id);
uint8_t vamp_add_device(const uint8_t* rf_id);
uint8_t vamp_get_vreg_device(const uint8_t * rf_id);

/** @brief Retornar el indice del dispositivo inactivo más antiguo o 
 * 	VAMP_MAX_DEVICES si no hay dispositivos inactivos */
uint8_t vamp_get_oldest_inactive(void);


/* ======================= FUNCIONES VAMP TABLES ======================== */

void vamp_gw_vreg_init(char * vreg_url, char * gw_id){


	/* Verificar que los parámetros son válidos */
	if (vreg_url == NULL || gw_id == NULL || 
	    strlen(vreg_url) >= VAMP_ENDPOINT_MAX_LEN || 
	    strlen(gw_id) >= VAMP_GW_ID_MAX_LEN) {

		#ifdef VAMP_DEBUG
		Serial.println("Parámetros inválidos para la inicialización de VAMP");
		#endif /* VAMP_DEBUG */
		return;
	}

	vamp_vreg_profile.method = VAMP_HTTP_METHOD_GET;
	if (vamp_vreg_profile.endpoint_resource) {
		free(vamp_vreg_profile.endpoint_resource);
	}
	vamp_vreg_profile.endpoint_resource = strdup(vreg_url);
	if (!vamp_vreg_profile.endpoint_resource) {
		#ifdef VAMP_DEBUG
		Serial.println("Error al asignar memoria para el recurso VREG");
		#endif /* VAMP_DEBUG */
		return;
	}

	if(vamp_vreg_profile.protocol_options) {
		free(vamp_vreg_profile.protocol_options);
	}

	char protocol_options[VAMP_PROTOCOL_OPTIONS_MAX_LEN];
	snprintf(protocol_options, sizeof(protocol_options), VAMP_HTTP_VREG_HEADERS, gw_id);

	vamp_vreg_profile.protocol_options = strdup(protocol_options);
	if (!vamp_vreg_profile.protocol_options) {
		#ifdef VAMP_DEBUG
		Serial.println("Error al asignar memoria para las opciones del protocolo VREG");
		#endif /* VAMP_DEBUG */
		return;
	}

	if(vamp_vreg_profile.query_params) {
		free(vamp_vreg_profile.query_params);
	}
	vamp_vreg_profile.query_params = (char *)malloc(VAMP_QUERY_PARAMS_VREG_LEN);
	if (!vamp_vreg_profile.query_params) {
		#ifdef VAMP_DEBUG
		Serial.println("Error al asignar memoria para los parámetros de consulta del VREG");
		#endif /* VAMP_DEBUG */
		return;
	}

}

// Inicializar todas las tablas VAMP con sincronización VREG
void vamp_table_update() {

	/* Verificar que los valores de los recursos no estén vacíos */
	if (vamp_vreg_profile.endpoint_resource[0] == '\0' || vamp_vreg_profile.protocol_options[0] == '\0') {
		#ifdef VAMP_DEBUG
		Serial.println("no gw resources defined");
		#endif /* VAMP_DEBUG */
		return;
	}

	/* Ver cuando se actualizó la tabla por última vez */
	if(!strcmp(last_table_update, VAMP_TABLE_INIT_TSMP)) {
		// La tabla no ha sido inicializada
		#ifdef VAMP_DEBUG
		Serial.println("init vamp table");
		#endif /* VAMP_DEBUG */

		/* Inicializar la tabla VAMP */
		for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
			vamp_table[i].status = VAMP_DEV_STATUS_FREE;
		}
	}

	/* Pasar en el query_params */
	snprintf(vamp_vreg_profile.query_params, VAMP_QUERY_PARAMS_VREG_LEN, "?last_update=%s", last_table_update);

	/* Enviar request usando TELL y recibir respuesta */
	if (vamp_iface_comm(&vamp_vreg_profile, req_resp_internet_buff, strlen(req_resp_internet_buff))) {

		Serial.println("Enviando solicitud de sincronización VREG...");
		Serial.print(req_resp_internet_buff);

		/* !!!!! Aqui se utiliza un buffer doble que habria que ver como se puede evitar, el problema es que la respuesta
		viene en req_resp_internet_buff que ya es bastante grande y se le pasa al parser de json el cual crea su propio
		buffer interno. Esto puede llevar a un uso excesivo de memoria y posibles problemas de rendimiento. */

		/* Extraer los datos JSON de la respuesta */
		if (vamp_process_sync_json_response(req_resp_internet_buff)) {
			Serial.println("Sincronización VREG completada exitosamente");
		} else {
			Serial.println("Error procesando respuesta VREG");
		}

		#ifdef VAMP_DEBUG
		vamp_process_sync_json_response(req_resp_internet_buff);
		#endif /* VAMP_DEBUG */

		return;
	}

	return;

}


uint8_t vamp_get_vreg_device(const uint8_t * rf_id) {

	/* Verificar que el RF_ID es válido */
	if (!vamp_is_rf_id_valid(rf_id)) {
		return VAMP_MAX_DEVICES;
	}

	/* Buffer para el ID del nodo, 10 caracteres (5 bytes en hex) + '/0' */
	char node_id[11];

	/* Convertir RF_ID a cadena hex */
	rf_id_to_hex(rf_id, node_id);

	// formamos la cadena para la solicitud de sincronización
	// el formato es: "VAMP_SYNC --gateway gateway_id --last_time last_update"
	snprintf(req_resp_internet_buff, sizeof(req_resp_internet_buff), 
		"%s %s %s %s %s", VAMP_GET_NODE, VAMP_GATEWAY_ID, vamp_gw_id, VAMP_NODE_ID, node_id);

	#ifdef VAMP_DEBUG
	Serial.print("Enviando ");
	Serial.println(req_resp_internet_buff);
	#endif /* VAMP_DEBUG */

	// Enviar request usando TELL y recibir respuesta
	if (vamp_iface_comm(&vamp_vreg_profile, req_resp_internet_buff, sizeof(req_resp_internet_buff))) {

		/* Primero hay que revisar se la respuesta es válida, mirando si contiene el prefijo esperado */
		if (!strstr(req_resp_internet_buff, VAMP_GET_NODE)){
			#ifdef VAMP_DEBUG
			Serial.println("No se recibió respuesta válida del VREG");
			#endif /* VAMP_DEBUG */
			return VAMP_MAX_DEVICES;
		}

		/* Si la respuesta contiene un error, mostrarlo */
		char * node_data = strstr(req_resp_internet_buff, VAMP_GW_SYNC);
		if (node_data) {

			/* Mover el puntero al inicio de los datos JSON,
			+1 para saltar el espacio después del prefijo */
			node_data = node_data + strlen(VAMP_GW_SYNC) + 1;

			/* Extraer los datos JSON de la respuesta, aqui deberia venir unos solo */
			if (vamp_process_sync_json_response(node_data)) {
				#ifdef VAMP_DEBUG
				Serial.println("Sincronización VREG completada exitosamente");
				#endif /* VAMP_DEBUG */
				/* Buscar el dispositivo en la tabla */
				return (vamp_find_device(rf_id));
			}
		}
	}
	#ifdef VAMP_DEBUG
	Serial.println("Error procesando respuesta VREG");
	#endif /* VAMP_DEBUG */
	return VAMP_MAX_DEVICES; // No se espera respuesta de datos
}













// ======================== FUNCIONES ID COMPACTO ========================

/* Generar byte de ID compacto (verification + index) para un RF_ID */
uint8_t vamp_generate_id_byte(const uint8_t table_index) {

	/** Generar verificación del puerto a partir del numero que ya este
		en el slot del indice para evitar que sea el mismo */
	uint8_t check_bits = VAMP_GET_VERIFICATION(vamp_table[table_index].wsn_id);
	check_bits++; // Aumentar para evitar que sea el mismo

	// Generar ID byte: 3 bits de verificación + 5 bits de índice
	return (VAMP_MAKE_ID_BYTE(check_bits, table_index));
}


/* Funciones de tabla */

/* Buscar dispositivo por RF_ID */
uint8_t vamp_find_device(const uint8_t * rf_id) {
  for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
    if (memcmp(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN) == 0) {
      return i;
    }
  }
  return VAMP_MAX_DEVICES; // No encontrado
}

/* Limpiar una entrada específica de la tabla */
void vamp_clear_entry(int index) {
  if (index < 0 || index >= VAMP_MAX_DEVICES) {
    return;
  }
  
  if (vamp_table[index].status != VAMP_DEV_STATUS_FREE) {
		// Liberar recursos asignados dinámicamente en todos los perfiles
		vamp_clear_device_profiles(index);
		
		// Liberar buffer de datos temporales si existe
		if (vamp_table[index].data_buff) {
			free(vamp_table[index].data_buff);
			vamp_table[index].data_buff = NULL;
		}
		
    // Solo marcar como libre - otros campos se sobrescriben cuando se reasigna
    vamp_table[index].status = VAMP_DEV_STATUS_FREE;
  }
}

/** @brief Agregar dispositivo a la tabla pero no lo configura completamente
 * 		Esta función solo agrega el dispositivo a la tabla y establece su estado
 * 		inicial. Esto es responsabilidad de otras funciones.
 */
uint8_t vamp_add_device(const uint8_t* rf_id) {

	/* Asegurarse que el dispositivo es válido */
	if (!vamp_is_rf_id_valid(rf_id)) {
		#ifdef VAMP_DEBUG
		Serial.println("RF_ID no válido");
		#endif /* VAMP_DEBUG */
		return VAMP_MAX_DEVICES;
	}

	uint8_t table_index;

	/* buscar un slot libre */
	for (table_index = 0; table_index < VAMP_MAX_DEVICES; table_index++) {
		if (vamp_table[table_index].status == VAMP_DEV_STATUS_FREE) {
			break;
		}
	}

	/* Si no hubiera slots libres, buscamos el sensor inactivo mas antiguo */
	if(table_index == VAMP_MAX_DEVICES) {
		table_index = vamp_get_oldest_inactive();

		if(table_index >= VAMP_MAX_DEVICES) {
			#ifdef VAMP_DEBUG
			Serial.println("No hay slots libres ni inactivos");
			#endif /* VAMP_DEBUG */
			return VAMP_MAX_DEVICES; // No hay espacio para un nuevo dispositivo
		}
	}

	/* Si encontramos un slot libre o inactivo */
	if (table_index < VAMP_MAX_DEVICES) {
		// Reemplazar el sensor inactivo más antiguo
		vamp_clear_entry(table_index);
		memcpy(vamp_table[table_index].rf_id, rf_id, VAMP_ADDR_LEN);
		vamp_table[table_index].wsn_id = vamp_generate_id_byte(table_index);
		vamp_table[table_index].status = VAMP_DEV_STATUS_ADDED;
		vamp_table[table_index].last_activity = millis();
		vamp_table[table_index].ticket = 0;

		/* Reservar memoria para el buffer de datos temporales. Estos datos se guardaran
		como una cadena asi que se debe tener en cuenta el terminador nulo */
		vamp_table[table_index].data_buff = (char * )malloc(VAMP_MAX_PAYLOAD_SIZE + 1);
		if (!vamp_table[table_index].data_buff) {
			#ifdef VAMP_DEBUG
			Serial.println("Error: No se pudo reservar memoria para data_buff");
			#endif /* VAMP_DEBUG */
			// Si falla la reserva, limpiar la entrada y retornar error
			vamp_table[table_index].status = VAMP_DEV_STATUS_FREE;
			return VAMP_MAX_DEVICES;
		}
		
		#ifdef VAMP_DEBUG
		Serial.print("Buffer de datos reservado para dispositivo en índice ");
		Serial.println(table_index);
		#endif /* VAMP_DEBUG */
	}
  
	return table_index;
}

/* Remover dispositivo de la tabla */
bool vamp_remove_device(const uint8_t* rf_id) {

	/* Asegurarse que el dispositivo está en la tabla */
	uint8_t index = vamp_find_device(rf_id);
  	if (index < VAMP_MAX_DEVICES) {
    	vamp_clear_entry(index);
    	return true;
    }
	
	return false; // No encontrado
}

/* Buscar dispositivos expirados para marcarlos como inactivos */
void vamp_detect_expired() {
  uint32_t current_time = millis();
  
  // Limpiar tabla unificada VAMP
  for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
    if (vamp_table[i].status == VAMP_DEV_STATUS_ACTIVE) {
      // Verificar timeout de dispositivo (que pasa cuando se desborda el millis()?????)
      if (current_time - vamp_table[i].last_activity > VAMP_DEVICE_TIMEOUT) {
        vamp_table[i].status = VAMP_DEV_STATUS_INACTIVE;
      }
    }
  }
}

uint8_t vamp_get_oldest_inactive(void){

	/* Buscar el dispositivo inactivo más antiguo, para ello cargamos primero el tiempo
	 pues actual, pues siempre cuaquier dispositivo inactivo tendrá un tiempo de última
	 actividad menor (será más antiguo)
	 OJO!!! esta funcion escrita asi no tiene en cuenta el desbordamiento del millis(),
	 en una refactorización futura se debe tener en cuenta este caso!!!!!
	 */
	uint32_t oldest_time = millis();
	uint8_t oldest_index = VAMP_MAX_DEVICES; // Valor por defecto si no se encuentra

	for (uint8_t i = 0; i < VAMP_MAX_DEVICES; i++) {
		if (vamp_table[i].status == VAMP_DEV_STATUS_INACTIVE) {
			if (vamp_table[i].last_activity < oldest_time) {
				oldest_time = vamp_table[i].last_activity;
				oldest_index = i;
			}
		}
	}

	return oldest_index; // Retorna el índice del dispositivo inactivo más antiguo o VAMP_MAX_DEVICES si no hay
}

/* Obtener entrada de la tabla VAMP y validarla a partir del campo wsn_id */
bool vamp_get_entry(vamp_entry_t * entry, uint8_t wsn_id) {

		/*  En el segundo byte se encuentra el ID del dispositivo
		 	en la forma de: Formato del byte ID: [VVV][IIIII] */
		entry = &vamp_table[VAMP_GET_INDEX(wsn_id)];

		/* Verificar que la verificación es correcta */
		if (entry->wsn_id != wsn_id) {
			#ifdef VAMP_DEBUG
			Serial.println("Verificación de dispositivo fallida");
			#endif /* VAMP_DEBUG */
			return false; // Verificación fallida
		}

		return true;
}









// ======================== PARSING RESPUESTAS VREG ========================



bool is_valid_rf_id(const char * rf_id) {
  // Verificar que el RF_ID no sea NULL y tenga la longitud correcta
  if (rf_id == NULL || strlen(rf_id) != VAMP_ADDR_LEN * 2) {
	return false;
  }
  
  // Verificar que todos los caracteres sean válidos (0-9, A-F)
  for (int i = 0; i < (VAMP_ADDR_LEN * 2); i++) {
    if (!((rf_id[i] >= '0' && rf_id[i] <= '9') || (rf_id[i] >= 'A' && rf_id[i] <= 'F'))) {
      return false;
    }
  }
  
  return true;
}


/* Convertir string hexadecimal a RF_ID */
bool hex_to_rf_id(const char * hex_str, uint8_t * rf_id) {
  if (!is_valid_rf_id(hex_str)) {
    return false;
  }

  /* Convertir cada par de caracteres a un byte */
  for (int i = 0; i < VAMP_ADDR_LEN; i++) {
    char hex_pair[3] = {0}; // Para almacenar 2 caracteres + '\0'
    hex_pair[0] = hex_str[i * 2];     // Primer dígito hex
    hex_pair[1] = hex_str[i * 2 + 1]; // Segundo dígito hex
    hex_pair[2] = '\0';               // Terminador
    
    // Convertir el par hex a byte usando strtol
    rf_id[i] = (uint8_t)strtol(hex_pair, NULL, 16);
  }
  
  return true;
}

void rf_id_to_hex(const uint8_t * rf_id, char * hex_str) {
  /* Convertir cada byte a un par de caracteres hexadecimales */
  for (int i = 0; i < VAMP_ADDR_LEN; i++) {
	snprintf(&hex_str[i * 2], 3, "%02X", rf_id[i]);
  }
  hex_str[VAMP_ADDR_LEN * 2] = '\0'; // Asegurar terminación
}


bool vamp_get_timestamp(char * timestamp) {

	if (!timestamp) {
		#ifdef VAMP_DEBUG
		Serial.println("Error: No se encontró timestamp en la respuesta VREG");
		#endif /* VAMP_DEBUG */
		return false;
	}

	// Encontrar el final del timestamp (siguiente espacio o final de línea o cadena)
	char * timestamp_end = strchr(timestamp, ' ');
	if (!timestamp_end) {
		timestamp_end = strchr(timestamp, '\n');
		if (!timestamp_end) {
			timestamp_end = timestamp + strlen(timestamp);
		}
	}

	uint8_t timestamp_length = strlen(VAMP_TABLE_INIT_TSMP); // Longitud esperada del timestamp

	if (timestamp_end - timestamp != timestamp_length) {
		#ifdef VAMP_DEBUG
		Serial.print("Timestamp inválido en la respuesta VREG: ");
		Serial.println(timestamp);
		#endif /* VAMP_DEBUG */
		return false;
	}

	//vamp_validate_timestamp(timestamp); // Validar el timestamp

	memcpy(last_table_update, timestamp, timestamp_length);
	last_table_update[timestamp_length] = '\0'; // Asegurar el null-terminator

	#ifdef VAMP_DEBUG
	Serial.print("updated at: ");
	Serial.println(last_table_update);
	#endif /* VAMP_DEBUG */

	return true;
}


/** 
 * Process the synchronization response from VREG.*/
bool vamp_process_sync_json_response(const char* json_data) {

	if (json_data == NULL) {
		return false;
	}

	/* Crear buffer JSON dinámico */
	DynamicJsonDocument doc(4096);
	
	/* Parsear el JSON */
	DeserializationError error = deserializeJson(doc, json_data);
	if (error) {
		#ifdef VAMP_DEBUG
		Serial.print("Error parseando JSON: ");
		Serial.println(error.c_str());
		#endif /* VAMP_DEBUG */
		return false;
	}

	/* Verificar que el JSON es un array */
	if (!doc.is<JsonArray>()) {
		#ifdef VAMP_DEBUG
		Serial.println("JSON no es un array válido");
		#endif /* VAMP_DEBUG */
		return false;
	}

	JsonArray nodes = doc.as<JsonArray>();
	
	/* Procesar cada entrada en el array de cambios */
	for (JsonObject node : nodes) {
		
		/* Verificar campos obligatorios y que no estén vacíos */
		if (!node.containsKey("rf_id") || !node["rf_id"] || 
			!node.containsKey("action") || !node["action"] ||
			!node.containsKey("type") || !node["type"] ||
			!node.containsKey("profiles") || !node["profiles"]) {
			#ifdef VAMP_DEBUG
			Serial.println("Entrada JSON sin mandatory fields o con valores vacíos");
			#endif /* VAMP_DEBUG */
			continue; // Saltar esta entrada
		}

		/* Extraer RF_ID */
		uint8_t rf_id[VAMP_ADDR_LEN];
		if (!hex_to_rf_id(node["rf_id"], rf_id)) {
			#ifdef VAMP_DEBUG
			Serial.println("RF_ID inválido en la respuesta VREG");
			#endif /* VAMP_DEBUG */
			continue;
		}

		#ifdef VAMP_DEBUG
		Serial.print("RF_ID recibido: ");
		Serial.println(node["rf_id"].as<String>());
		#endif /* VAMP_DEBUG */

		/* Buscar si el nodo ya esta en la tabla */
		uint8_t table_index = vamp_find_device(rf_id);
		if (table_index < VAMP_MAX_DEVICES) {
			#ifdef VAMP_DEBUG
			Serial.print("Nodo registrado ");
			Serial.println(node["rf_id"].as<String>());
			#endif /* VAMP_DEBUG */
		}
		if (table_index > VAMP_MAX_DEVICES) {
			#ifdef VAMP_DEBUG
			Serial.print("Error buscando al nodo en la tabla");
			#endif /* VAMP_DEBUG */
			continue;
		}

		/* Procesar según la acción */
		if (strcmp(node["action"], "ADD") == 0 || strcmp(node["action"], "UPDATE") == 0) {

			/* !!! a partir de aqui el nodo aun cuando estubiera agregado, se actualiza
			hay que ver lo conveniente o seguro de esta operacion, puede que sea mejor 
			no hacer nada si el nodo ya existe y esta activo..... */

			/* Si el nodo no está registrado, se agrega */
			if (table_index == VAMP_MAX_DEVICES) {
				table_index = vamp_add_device(rf_id);
			}
			/* Si el nodo ya está registrado, se actualiza, para eso
			vamos a limpiar primero la entrada */
			else {
				vamp_clear_entry(table_index);
			}

			if (table_index >= VAMP_MAX_DEVICES) {
				#ifdef VAMP_DEBUG
				Serial.print("Error agregando nodo a la tabla: ");
				Serial.print(node["rf_id"].as<String>());
				Serial.println(" - Sin slots disponibles o error interno");
				#endif /* VAMP_DEBUG */
				continue; // Saltar este dispositivo y continuar con el siguiente
			}

			#ifdef VAMP_DEBUG
			Serial.print("Nodo agregado ");
			Serial.println(node["rf_id"].as<String>());
			#endif /* VAMP_DEBUG */

			/* Asignar el estado de cache */
			vamp_table[table_index].status = VAMP_DEV_STATUS_CACHE;

			/* Extraer tipo del dispositivo */		
			if (!strcmp(node["type"], "fixed")) {
				vamp_table[table_index].type = 0;
			} else if (!strcmp(node["type"], "dynamic")) {
				vamp_table[table_index].type = 1;
			} else if (!strcmp(node["type"], "auto")) {
				vamp_table[table_index].type = 2;
			} else {
				/* !!!!! valor por defecto ???? */
				#ifdef VAMP_DEBUG
				Serial.print("Tipo de dispositivo desconocido: ");
				Serial.println(node["type"].as<String>());
				#endif /* VAMP_DEBUG */
				vamp_table[table_index].type = 0; // Valor por defecto
			}

			/* Procesar perfiles, debe haber al menos uno */
			JsonArray profiles = node["profiles"];
			if (profiles.size() == 0 || profiles.size() >= VAMP_MAX_PROFILES) {
				#ifdef VAMP_DEBUG
				Serial.print("Tiene que haber entre 1 y ");
				Serial.print(VAMP_MAX_PROFILES);
				Serial.println(" perfiles");
				#endif /* VAMP_DEBUG */
				continue; // Saltar este nodo si no hay perfiles
			}

			/* Inicializar contador de perfiles */
			uint8_t profile_index = 0;
			vamp_table[table_index].profile_count = 0;

			/* Procesar cada perfil */
			for (JsonObject profile : profiles) {
				if (profile_index >= VAMP_MAX_PROFILES) {
					#ifdef VAMP_DEBUG
					Serial.println("Límite máximo de perfiles alcanzado");
					#endif /* VAMP_DEBUG */
					break;
				}

				/* Extraer method */
				if (profile.containsKey("method")) {

					/* Extraer el metodo (GET, POST...) */
					if (!strcmp(profile["method"], "GET")) {
						vamp_table[table_index].profiles[profile_index].method = VAMP_HTTP_METHOD_GET;
					} else if (!strcmp(profile["method"], "POST")) {
						vamp_table[table_index].profiles[profile_index].method = VAMP_HTTP_METHOD_POST;
					} else if (!strcmp(profile["method"], "PUT")) {
						vamp_table[table_index].profiles[profile_index].method = VAMP_HTTP_METHOD_PUT;
					} else if (!strcmp(profile["method"], "DELETE")) {
						vamp_table[table_index].profiles[profile_index].method = VAMP_HTTP_METHOD_DELETE;
					} else {
						#ifdef VAMP_DEBUG
						Serial.print("Método desconocido: ");
						Serial.println(profile["method"].as<String>());
						#endif /* VAMP_DEBUG */
						/* !!! no me gustan estos valores por defecto !!!  */
						vamp_table[table_index].profiles[profile_index].method = 0; // Valor por defecto
					}
				} else {
					/* !!! no me gustan estos valores por defecto !!!  */
					vamp_table[table_index].profiles[profile_index].method = 0; // Valor por defecto
				}

				/* Extraer endpoint_resource */
				if (profile.containsKey("endpoint")) {
					const char* endpoint_str = profile["endpoint"];
					if (endpoint_str && strlen(endpoint_str) > 0 && strlen(endpoint_str) < VAMP_ENDPOINT_MAX_LEN) {
						/* Liberar memoria previa si existe */
						if (vamp_table[table_index].profiles[profile_index].endpoint_resource) {
							free(vamp_table[table_index].profiles[profile_index].endpoint_resource);
						}
						/* Asignar nueva memoria */
						vamp_table[table_index].profiles[profile_index].endpoint_resource = strdup(endpoint_str);
						if (!vamp_table[table_index].profiles[profile_index].endpoint_resource) {
							#ifdef VAMP_DEBUG
							Serial.println("Error asignando memoria para endpoint_resource");
							#endif /* VAMP_DEBUG */
							break;
						}
					} else {
						#ifdef VAMP_DEBUG
						Serial.println("Endpoint resource inválido o demasiado largo");
						#endif /* VAMP_DEBUG */
					}
				}

				/* Extraer protocol_options */
				if (profile.containsKey("options")) {
					const char* options_str = profile["options"];
					if (options_str && strlen(options_str) > 0 && strlen(options_str) < VAMP_PROTOCOL_OPTIONS_MAX_LEN) {
						/* Liberar memoria previa si existe */
						if (vamp_table[table_index].profiles[profile_index].protocol_options) {
							free(vamp_table[table_index].profiles[profile_index].protocol_options);
						}
						/* Asignar nueva memoria */
						vamp_table[table_index].profiles[profile_index].protocol_options = strdup(options_str);
						if (!vamp_table[table_index].profiles[profile_index].protocol_options) {
							#ifdef VAMP_DEBUG
							Serial.println("Error asignando memoria para protocol_options");
							#endif /* VAMP_DEBUG */
							break;
						}
					} else {
						#ifdef VAMP_DEBUG
						Serial.println("Protocol options inválido o demasiado largo");
						#endif /* VAMP_DEBUG */
					}
				}

				/* Extraer los protocols query */
				if (profile.containsKey("params")) {
					const char* query_str = profile["params"];
					if (query_str && strlen(query_str) > 0 && strlen(query_str) < VAMP_PROTOCOL_OPTIONS_MAX_LEN) {
						/* Liberar memoria previa si existe */
						if (vamp_table[table_index].profiles[profile_index].query_params) {
							free(vamp_table[table_index].profiles[profile_index].query_params);
						}
						/* Asignar nueva memoria */
						vamp_table[table_index].profiles[profile_index].query_params = strdup(query_str);
						if (!vamp_table[table_index].profiles[profile_index].query_params) {
							#ifdef VAMP_DEBUG
							Serial.println("Error asignando memoria para query_params");
							#endif /* VAMP_DEBUG */
							break;
						}
					} else {
						#ifdef VAMP_DEBUG
						Serial.println("Protocol query inválido o demasiado largo");
						#endif /* VAMP_DEBUG */
					}
				}

				profile_index++;
				vamp_table[table_index].profile_count = profile_index;
			}

			#ifdef VAMP_DEBUG
			Serial.print("Dispositivo ADD procesado con ");
			Serial.print(profile_index);
			Serial.println(" perfiles");
			#endif /* VAMP_DEBUG */


		} else if (strcmp(node["action"], "REMOVE") == 0) {

			if (table_index == VAMP_MAX_DEVICES) {
				/* Remover dispositivo de la tabla */
				vamp_clear_entry(table_index);
				#ifdef VAMP_DEBUG
				Serial.println("Dispositivo REMOVE procesado exitosamente");
				#endif /* VAMP_DEBUG */
			} else {
				#ifdef VAMP_DEBUG
				Serial.println("Dispositivo REMOVE no encontrado en tabla");
				#endif /* VAMP_DEBUG */
			}

		} else {
			#ifdef VAMP_DEBUG
			Serial.print("Acción desconocida en JSON: ");
			Serial.println(node["action"].as<const char*>());
			#endif /* VAMP_DEBUG */
		}
	}

	return true; // Procesamiento exitoso
}


/* --------------- WSN --------------- */

bool vamp_gw_wsn(void) {

	/* Buffer para datos WSN */
	static uint8_t wsn_buffer[VAMP_MAX_PAYLOAD_SIZE];

    /* Extraer el mensaje de la interface via callback */
	uint8_t data_recv = vamp_wsn_comm(wsn_buffer, VAMP_MAX_PAYLOAD_SIZE);

	if (data_recv == 0) {
		return false; // No hay datos disponibles
	}		

	/* Inicializar el puntero a la entrada 
	!!!!! esto no es del todo correcto asi !!!!! */
	vamp_entry_t * entry = NULL;

	/* --------------------- Si es un comando --------------------- */
	if (wsn_buffer[0] & VAMP_IS_CMD_MASK) {

		/* Aislar el comando */
		wsn_buffer[0] = wsn_buffer[0] & VAMP_WSN_CMD_MASK;
		uint8_t table_index = 0;


		/* Procesar el comando */
		switch (wsn_buffer[0]) {
			case VAMP_JOIN_REQ:
				/* Manejar el comando JOIN_REQ */
				#ifdef VAMP_DEBUG
				Serial.println("Comando JOIN_REQ recibido");
				#endif /* VAMP_DEBUG */

				/* Verificamos que tenga el largo correcto */
				if (data_recv != VAMP_JOIN_REQ_LEN) {
					#ifdef VAMP_DEBUG
					Serial.println("JOIN_REQ inválido, largo incorrecto");
					#endif /* VAMP_DEBUG */
					return false; // Comando inválido
				}
				table_index = vamp_find_device(&wsn_buffer[1]); // Asumimos que el RF_ID está después del comando

				if (table_index == VAMP_MAX_DEVICES) {
					#ifdef VAMP_DEBUG
					Serial.println("No dev en cache");
					#endif /* VAMP_DEBUG */

					/* Si no esta en el cache hay que preguntarle al VREG */
					table_index = vamp_get_vreg_device(&wsn_buffer[1]);
					if (table_index == VAMP_MAX_DEVICES) {
						#ifdef VAMP_DEBUG
						Serial.println("Error al obtener el dispositivo del VREG");
						#endif /* VAMP_DEBUG */
						return false; // Error al obtener el dispositivo
					}

					#ifdef VAMP_DEBUG
					Serial.print("Dispositivo encontrado en VREG, index: ");
					Serial.println(table_index);
					#endif /* VAMP_DEBUG */

					/* Formamos la respuesta para el nodo solicitante */
					wsn_buffer[0] = VAMP_JOIN_ACK | VAMP_IS_CMD_MASK;
					wsn_buffer[1] = table_index;
					for (int i = 0; i < VAMP_ADDR_LEN; i++) {
						wsn_buffer[i + 2] = (uint8_t)vamp_gw_id[i]; // Asignar el ID del gateway
					}
					/* Reportamos al nodo solicitante */
					vamp_wsn_comm(vamp_table[table_index].rf_id, wsn_buffer, 2 + VAMP_ADDR_LEN);

				} else {
					#ifdef VAMP_DEBUG
					Serial.print("Dispositivo encontrado en cache, index: ");
					Serial.println(vamp_table[table_index].wsn_id);
					#endif /* VAMP_DEBUG */

					/* Formamos la respuesta para el nodo solicitante */
					/* El comando JOIN_ACK es 0x02, byte completo es 0x82 */
					wsn_buffer[0] = VAMP_JOIN_ACK | VAMP_IS_CMD_MASK;
					/* Enviar el identificador del nodo WSN en el gateway */
					wsn_buffer[1] = vamp_table[table_index].wsn_id; // Asignar el ID del nodo WSN
					/* Asignar el ID del gateway */
					for (int i = 0; i < VAMP_ADDR_LEN; i++) {
						uint8_t * local_wsn_addr = vamp_get_local_wsn_addr();
						wsn_buffer[i + 2] = (uint8_t)local_wsn_addr[i]; // Asignar el ID del gateway
					}
					/* Reportamos al nodo solicitante */
					if (vamp_wsn_comm(vamp_table[table_index].rf_id, wsn_buffer, 2 + VAMP_ADDR_LEN)) {
						vamp_table[table_index].status = VAMP_DEV_STATUS_ACTIVE; // Marcar como activo
						vamp_table[table_index].last_activity = millis(); // Actualizar última actividad
					} 
				}

				break;

			case VAMP_JOIN_ACK:
				#ifdef VAMP_DEBUG
				Serial.println("Comando JOIN_ACK recibido");
				#endif /* VAMP_DEBUG */
				break;
			case VAMP_PING:
				/* Manejar el comando PING */
				#ifdef VAMP_DEBUG
				Serial.println("Comando PING recibido");
				#endif /* VAMP_DEBUG */
				break;
			case VAMP_PONG:
				/* Manejar el comando PONG */
				#ifdef VAMP_DEBUG
				Serial.println("Comando PONG recibido");
				#endif /* VAMP_DEBUG */
				break;
			case VAMP_POLL:
				/* Manejar el comando POLL */
				#ifdef VAMP_DEBUG
				Serial.println("Comando POLL recibido");
				#endif /* VAMP_DEBUG */

				/* buscar la entrada */
				entry = NULL;
				if (!vamp_get_entry(entry, wsn_buffer[1])) {
					#ifdef VAMP_DEBUG
					Serial.println("Entrada no encontrada");
					#endif /* VAMP_DEBUG */
					return false; // Entrada no encontrada
				}

				/* Despues viene el ticket */
				if ((wsn_buffer[2] | (wsn_buffer[3] << 8)) == entry->ticket) {

					vamp_client_tell((uint8_t *)entry->data_buff, strlen(entry->data_buff) - 1);

					/* Si el ticket coincide, se puede procesar la solicitud */
					#ifdef VAMP_DEBUG
					Serial.print("Ticket found");
					Serial.println((wsn_buffer[2] | (wsn_buffer[3] << 8)));
					#endif /* VAMP_DEBUG */
				} else {
					/* Si el ticket no coincide, se ignora la solicitud */
					#ifdef VAMP_DEBUG
					Serial.println("ticket deprecate");
					#endif /* VAMP_DEBUG */
				}

				break;
			default:
				/* Comando desconocido */
				#ifdef VAMP_DEBUG
				Serial.print("Comando desconocido: 0x");
				Serial.println(wsn_buffer[0], HEX);
				#endif /* VAMP_DEBUG */


				break;
		}

	/* -------------- Si es un dato -------------- */
	} else {

		#ifdef VAMP_DEBUG
		Serial.println("Datos recibidos del WSN");
		#endif /* VAMP_DEBUG */

		/* El primer byte contiene el protocolo: [C=0][PP][LLLLL] */
		uint8_t profile_index = VAMP_WSN_GET_PROFILE(wsn_buffer[0]);
		uint8_t rec_len = VAMP_WSN_GET_LENGTH(wsn_buffer[0]);
		
		/* Manejar escape de longitud (si length == 31, el siguiente byte tiene la longitud real) */
		uint8_t data_offset = 2; // Por defecto: [protocol][wsn_id][data...]
		if (rec_len == VAMP_WSN_LENGTH_ESCAPE) {
			rec_len = wsn_buffer[2];
			data_offset = 3; // [protocol][wsn_id][length][data...]
		}

		/* Verificar que la longitud es válida */
		if ((rec_len > VAMP_MAX_PAYLOAD_SIZE - data_offset) || ((rec_len + data_offset) != data_recv)) {
			#ifdef VAMP_DEBUG
			Serial.println("Longitud de datos inválida");
			#endif /* VAMP_DEBUG */
			return false; // Longitud inválida
		}

		/* Verificar que el perfil es válido */
		if (profile_index >= VAMP_MAX_PROFILES) {
			#ifdef VAMP_DEBUG
			Serial.println("Índice de perfil inválido");
			#endif /* VAMP_DEBUG */
			return false; // Perfil inválido
		}

		if (!vamp_get_entry(entry, wsn_buffer[1])) {
			#ifdef VAMP_DEBUG
			Serial.println("Entrada no encontrada");
			#endif /* VAMP_DEBUG */
			return false; // Entrada no encontrada
		}

		/* Verificar que el dispositivo tiene el perfil solicitado */
		const vamp_profile_t* profile = &entry->profiles[profile_index];
		if (!profile) {
			#ifdef VAMP_DEBUG
			Serial.print("Perfil ");
			Serial.print(profile_index);
			Serial.println(" no configurado para este dispositivo");
			#endif /* VAMP_DEBUG */
			return false; // Perfil no configurado
		}

		/* Actualizar la última actividad del dispositivo */
		entry->last_activity = millis();

		#ifdef VAMP_DEBUG
		Serial.print("dev: ");
		Serial.print(entry->wsn_id);
		Serial.print(" profile: ");
		Serial.print(profile_index);
		Serial.print(" resource: ");
		Serial.print(profile->endpoint_resource);
		Serial.print(" data: ");
		vamp_debug_msg(&wsn_buffer[data_offset], rec_len);
		#endif /* VAMP_DEBUG */

		/** Como la respuesta del servidor puede demorar y 
		probablemente el mote no resuelva nada con ella
		le respondemos y ack para que el mote sepa que 
		al menos su tarea fue recibida 
		@note que por cada ACK que se envie se incrementa el ticket
		y como en este caso hay un solo buffer, pues se recuerda un
		solo ticket por cada comunicación, si no se hace polling el 
		ticket se pierde.
		*/		
		entry->ticket++;
		uint8_t ack_buffer[3] = { (uint8_t)(VAMP_ACK | VAMP_IS_CMD_MASK), (uint8_t)(entry->ticket & 0xFF), (uint8_t)((entry->ticket >> 8) & 0xFF) };
		vamp_wsn_comm(entry->rf_id, ack_buffer, 3);


		/* Copiar los datos recibidos al buffer de internet si es que hay datos */
		if(rec_len > 0) {
			memcpy(req_resp_internet_buff, &wsn_buffer[data_offset], rec_len);
		}
		req_resp_internet_buff[rec_len] = '\0'; // Asegurar terminación de cadena si es necesario

		/* Enviar con el perfil completo (método/endpoint/params) */
		if (profile->endpoint_resource && profile->endpoint_resource[0] != '\0') {
			if(vamp_iface_comm(profile, req_resp_internet_buff, rec_len)) {
				#ifdef VAMP_DEBUG
				Serial.println("Datos enviados a internet");
				#endif /* VAMP_DEBUG */
				
				/* !!!!!! hay que ver que pasa si se puede enviar y si hay respuesta */

				//hay que dejar fuera el '\0'
				memcpy(entry->data_buff, req_resp_internet_buff, rec_len); // Copiar datos al buffer del dispositivo

				return true;
			}
		} else {
			#ifdef VAMP_DEBUG
			Serial.println("Endpoint resource vacío, no se envía a internet");
			#endif /* VAMP_DEBUG */
		}


	}

	return true; // Procesamiento exitoso
}

/* --------------------- Funciones públicas tabla VAMP -------------------- */

/** @brief Obtener timestamp de la última sincronización */
const char* vamp_get_last_sync_timestamp(void) {
    return last_table_update;
}

/** @brief Verificar si la tabla ha sido inicializada */
bool vamp_is_table_initialized(void) {
    return strcmp(last_table_update, VAMP_TABLE_INIT_TSMP) != 0;
}

/** @brief Obtener número de dispositivos activos en la tabla */
uint8_t vamp_get_device_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < VAMP_MAX_DEVICES; i++) {
        if (vamp_table[i].status != VAMP_DEV_STATUS_FREE) {
            count++;
        }
    }
    return count;
}

/** @brief Obtener entrada de la tabla por índice */
const vamp_entry_t* vamp_get_table_entry(uint8_t index) {
    if (index >= VAMP_MAX_DEVICES) {
        return NULL;
    }
    
    // Solo retornar entradas no libres
    if (vamp_table[index].status == VAMP_DEV_STATUS_FREE) {
        return NULL;
    }
    
    return &vamp_table[index];
}

/* --------------------- Funciones para manejo de múltiples perfiles -------------------- */
/** @todo REVISAR O LA PERTINENCIA DE ESTAS FUNCIONES O ELIMINAR */
/** @brief Obtener perfil específico de un dispositivo */
const vamp_profile_t* vamp_get_device_profile(uint8_t device_index, uint8_t profile_index) {
    if (device_index >= VAMP_MAX_DEVICES || profile_index >= vamp_table[device_index].profile_count) {
        return NULL;
    }
    
    // No devolver perfiles de dispositivos libres
    if (vamp_table[device_index].status == VAMP_DEV_STATUS_FREE) {
        return NULL;
    }
    
    return &vamp_table[device_index].profiles[profile_index];
}

/** @brief Configurar perfil específico de un dispositivo */
bool vamp_set_device_profile(uint8_t device_index, uint8_t profile_index, const vamp_profile_t* profile) {
    if (device_index >= VAMP_MAX_DEVICES || profile_index >= VAMP_MAX_PROFILES || !profile) {
        return false;
    }
    
    // No configurar perfiles en dispositivos libres
    if (vamp_table[device_index].status == VAMP_DEV_STATUS_FREE) {
        return false;
    }
    
    // Liberar recursos previos si existen
    if (vamp_table[device_index].profiles[profile_index].endpoint_resource) {
        free(vamp_table[device_index].profiles[profile_index].endpoint_resource);
        vamp_table[device_index].profiles[profile_index].endpoint_resource = NULL;
    }
    if (vamp_table[device_index].profiles[profile_index].protocol_options) {
        free(vamp_table[device_index].profiles[profile_index].protocol_options);
        vamp_table[device_index].profiles[profile_index].protocol_options = NULL;
    }
    
    // Configurar el nuevo perfil
    //vamp_table[device_index].profiles[profile_index].protocol = profile->protocol;
    vamp_table[device_index].profiles[profile_index].method = profile->method;
    
    // Copiar endpoint_resource si no es NULL
    if (profile->endpoint_resource) {
        size_t len = strlen(profile->endpoint_resource);
        vamp_table[device_index].profiles[profile_index].endpoint_resource = (char*)malloc(len + 1);
        if (vamp_table[device_index].profiles[profile_index].endpoint_resource) {
            strcpy(vamp_table[device_index].profiles[profile_index].endpoint_resource, profile->endpoint_resource);
        }
    }
    
    // Copiar protocol_options si no es NULL
    if (profile->protocol_options) {
        size_t len = strlen(profile->protocol_options);
        vamp_table[device_index].profiles[profile_index].protocol_options = (char*)malloc(len + 1);
        if (vamp_table[device_index].profiles[profile_index].protocol_options) {
            strcpy(vamp_table[device_index].profiles[profile_index].protocol_options, profile->protocol_options);
        }
    }
    
    // Actualizar profile_count si es necesario
    if (profile_index >= vamp_table[device_index].profile_count) {
        vamp_table[device_index].profile_count = profile_index + 1;
    }
    
    return true;
}

/** @brief Limpiar todos los perfiles de un dispositivo */
void vamp_clear_device_profiles(uint8_t device_index) {
    if (device_index >= VAMP_MAX_DEVICES) {
        return;
    }
    
    // Liberar memoria de todos los perfiles
    for (uint8_t i = 0; i < VAMP_MAX_PROFILES; i++) {
        if (vamp_table[device_index].profiles[i].endpoint_resource) {
            free(vamp_table[device_index].profiles[i].endpoint_resource);
            vamp_table[device_index].profiles[i].endpoint_resource = NULL;
        }
        if (vamp_table[device_index].profiles[i].protocol_options) {
            free(vamp_table[device_index].profiles[i].protocol_options);
            vamp_table[device_index].profiles[i].protocol_options = NULL;
        }
        //vamp_table[device_index].profiles[i].protocol = 0;
        vamp_table[device_index].profiles[i].method = 0;
    }
    
    vamp_table[device_index].profile_count = 0;
}

/* --------------------- Funciones para manejo de protocolos -------------------- */

/** @brief Obtener nombre legible del protocolo */
/* const char* vamp_get_protocol_string(uint8_t protocol) {
    switch (protocol) {
        case VAMP_PROTOCOL_HTTP:
            return "HTTP";
        case VAMP_PROTOCOL_HTTPS:
            return "HTTPS";
        case VAMP_PROTOCOL_MQTT:
            return "MQTT";
        case VAMP_PROTOCOL_COAP:
            return "CoAP";
        case VAMP_PROTOCOL_WEBSOCKET:
            return "WebSocket";
        case VAMP_PROTOCOL_CUSTOM:
            return "Custom";
        default:
            return "Unknown";
    }
} */

/** @brief Enviar datos usando el protocolo específico del perfil */
/* uint8_t vamp_send_with_profile(const vamp_profile_t* profile, char* data, size_t len) {
    if (!profile || !profile->endpoint_resource) {
        return 0;
    }
    
    switch (profile->protocol) {
        case VAMP_PROTOCOL_HTTP:
        case VAMP_PROTOCOL_HTTPS:
            // Usar la implementación HTTP existente
            return vamp_iface_comm(profile->endpoint_resource, data, len);
            
        case VAMP_PROTOCOL_MQTT:
            // TODO: Implementar MQTT
            #ifdef VAMP_DEBUG
            Serial.println("MQTT no implementado aún");
            #endif
            return 0;
            
        case VAMP_PROTOCOL_COAP:
            // TODO: Implementar CoAP  
            #ifdef VAMP_DEBUG
            Serial.println("CoAP no implementado aún");
            #endif
            return 0;
            
        case VAMP_PROTOCOL_WEBSOCKET:
            // TODO: Implementar WebSocket
            #ifdef VAMP_DEBUG
            Serial.println("WebSocket no implementado aún");
            #endif
            return 0;
            
        case VAMP_PROTOCOL_CUSTOM:
            // TODO: Permitir protocolos definidos por usuario
            #ifdef VAMP_DEBUG
            Serial.println("Protocolo custom no implementado aún");
            #endif
            return 0;
            
        default:
            #ifdef VAMP_DEBUG
            Serial.print("Protocolo desconocido: ");
            Serial.println(profile->protocol);
            #endif
            return 0;
    }
} */

