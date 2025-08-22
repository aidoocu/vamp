/** @brief VAMP Gateway
 * 
 * 
 * 
 * 
 */
#include "vamp_gw.h"
#include "vamp_client.h"
#include "vamp_callbacks.h"

#ifdef ARDUINO_ARCH_ESP8266
#include "lib/vamp_json.cpp"
#include <cstring>
#include <cstdlib>
#endif

#ifdef  __has_include
	#if __has_include(<ArduinoJson.h>)
		#define ARDUINOJSON_AVAILABLE
	#endif
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

	// Inicializar y configurar protocol_options
	vamp_kv_init(&vamp_vreg_profile.protocol_options);
	vamp_kv_set(&vamp_vreg_profile.protocol_options, "Accept", "application/json");
	vamp_kv_set(&vamp_vreg_profile.protocol_options, "X-VAMP-Gateway-ID", gw_id);

	// Inicializar query_params (se configurará dinámicamente en vamp_table_update)
	vamp_kv_init(&vamp_vreg_profile.query_params);

}

// Inicializar todas las tablas VAMP con sincronización VREG
void vamp_table_update() {

	/* Verificar que los valores de los recursos no estén vacíos (!!!! No estoy seguro que la validación correcta !!!!) */
	if (vamp_vreg_profile.endpoint_resource[0] == '\0' || vamp_vreg_profile.protocol_options.count == 0) {
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

	/* Configurar query_params con last_update */
	vamp_kv_clear(&vamp_vreg_profile.query_params);
	vamp_kv_set(&vamp_vreg_profile.query_params, "last_update", last_table_update);

	/* Enviar request usando TELL y recibir respuesta */
	if (vamp_iface_comm(&vamp_vreg_profile, req_resp_internet_buff, strlen(req_resp_internet_buff))) {

		Serial.println("Enviando solicitud de sincronización VREG...");
		Serial.print(req_resp_internet_buff);

		/* !!!!! Aqui se utiliza un buffer doble que habria que ver como se puede evitar, el problema es que la respuesta
		viene en req_resp_internet_buff que ya es bastante grande y se le pasa al parser de json el cual crea su propio
		buffer interno. Esto puede llevar a un uso excesivo de memoria y posibles problemas de rendimiento. */

		/* Extraer los datos JSON de la respuesta */
		#ifdef ARDUINOJSON_AVAILABLE
		if (vamp_process_sync_json_response(req_resp_internet_buff)) {
			Serial.println("Sincronización VREG completada exitosamente");
		} else {
			Serial.println("Error procesando respuesta VREG");
		}
		#endif /* ARDUINOJSON_AVAILABLE */

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

			#ifdef ARDUINOJSON_AVAILABLE
			/* Extraer los datos JSON de la respuesta, aqui deberia venir unos solo */
			if (vamp_process_sync_json_response(node_data)) {
				#ifdef VAMP_DEBUG
				Serial.println("Sincronización VREG completada exitosamente");
				#endif /* VAMP_DEBUG */
				/* Buscar el dispositivo en la tabla */
				return (vamp_find_device(rf_id));
			}
			#endif /* ARDUINOJSON_AVAILABLE */
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
				if ((uint16_t)(wsn_buffer[2] | (wsn_buffer[3] << 8)) == entry->ticket) {

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
    // Limpiar key-value stores
    vamp_kv_clear(&vamp_table[device_index].profiles[profile_index].protocol_options);
    vamp_kv_clear(&vamp_table[device_index].profiles[profile_index].query_params);
    
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
    
    // Copiar protocol_options - usar memcpy para copiar toda la estructura
    memcpy(&vamp_table[device_index].profiles[profile_index].protocol_options, 
           &profile->protocol_options, sizeof(vamp_key_value_store_t));
    
    // Copiar query_params - usar memcpy para copiar toda la estructura
    memcpy(&vamp_table[device_index].profiles[profile_index].query_params, 
           &profile->query_params, sizeof(vamp_key_value_store_t));
    
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
        vamp_clear_profile(&vamp_table[device_index].profiles[i]);
    }
    
    vamp_table[device_index].profile_count = 0;
}

/** @brief Limpiar un perfil específico liberando memoria */
void vamp_clear_profile(vamp_profile_t* profile) {
    if (!profile) return;
    
    // Liberar endpoint_resource si existe
    if (profile->endpoint_resource) {
        free(profile->endpoint_resource);
        profile->endpoint_resource = NULL;
    }
    
    // Limpiar key-value stores
    vamp_kv_free(&profile->protocol_options);
    vamp_kv_free(&profile->query_params);
    
    // Limpiar otros campos
    profile->method = 0;
}



/* ================= FUNCIONES PARA MANEJO DE KEY-VALUE PAIRS ================= */

/** @brief Inicializar un store de key-value */
void vamp_kv_init(vamp_key_value_store_t* store) {
    if (!store) return;
    store->pairs = NULL;
    store->count = 0;
    store->capacity = 0;
}

/** @brief Liberar memoria de un store de key-value */
void vamp_kv_free(vamp_key_value_store_t* store) {
    if (!store) return;
    if (store->pairs) {
        free(store->pairs);
        store->pairs = NULL;
    }
    store->count = 0;
    store->capacity = 0;
}

/** @brief Añadir o actualizar un par key-value */
bool vamp_kv_set(vamp_key_value_store_t* store, const char* key, const char* value) {
    if (!store || !key || !value) return false;
    
    if (strlen(key) >= VAMP_KEY_MAX_LEN || strlen(value) >= VAMP_VALUE_MAX_LEN) {
        return false; // Key o value demasiado largo
    }

	Serial.print("Añadiendo/actualizando key-value: ");
	Serial.print(key);
	Serial.print(" = ");
	Serial.println(value);
	Serial.print("Total pares actuales: ");
	Serial.println(store->count);

    // Buscar si la key ya existe
    for (uint8_t i = 0; i < store->count; i++) {
        if (strcmp(store->pairs[i].key, key) == 0) {
            // Actualizar valor existente
            strncpy(store->pairs[i].value, value, VAMP_VALUE_MAX_LEN - 1);
            store->pairs[i].value[VAMP_VALUE_MAX_LEN - 1] = '\0';
            return true;
        }
    }
    
    // Necesita expandir la capacidad?
    if (store->count >= store->capacity) {
        uint8_t new_capacity = (store->capacity == 0) ? 1 : store->capacity * 2;
        if (new_capacity > VAMP_MAX_KEY_VALUE_PAIRS) {
            new_capacity = VAMP_MAX_KEY_VALUE_PAIRS;
        }
        
        if (store->count >= new_capacity) {
            return false; // Límite máximo alcanzado
        }
        
        // Reallocar memoria
        vamp_key_value_pair_t* new_pairs = (vamp_key_value_pair_t*)realloc(
            store->pairs, new_capacity * sizeof(vamp_key_value_pair_t));
        if (!new_pairs) {
            return false; // Error de memoria
        }
        
        store->pairs = new_pairs;
        store->capacity = new_capacity;
        
        // Limpiar nueva memoria
        memset(&store->pairs[store->count], 0, 
               (new_capacity - store->count) * sizeof(vamp_key_value_pair_t));
    }
    
    // Añadir nueva entrada
    strncpy(store->pairs[store->count].key, key, VAMP_KEY_MAX_LEN - 1);
    store->pairs[store->count].key[VAMP_KEY_MAX_LEN - 1] = '\0';
    strncpy(store->pairs[store->count].value, value, VAMP_VALUE_MAX_LEN - 1);
    store->pairs[store->count].value[VAMP_VALUE_MAX_LEN - 1] = '\0';
    store->count++;

	Serial.print("Añadido key-value: ");
	Serial.print(key);
	Serial.print(" = ");
	Serial.println(value);
	Serial.print("Total pares: ");
	Serial.println(store->count);

    return true;
}

/** @brief Obtener valor por clave */
const char* vamp_kv_get(const vamp_key_value_store_t* store, const char* key) {
    if (!store || !key) return NULL;
    
    for (uint8_t i = 0; i < store->count; i++) {
        if (strcmp(store->pairs[i].key, key) == 0) {
            return store->pairs[i].value;
        }
    }
    return NULL; // No encontrado
}

/** @brief Verificar si existe una clave */
bool vamp_kv_exists(const vamp_key_value_store_t* store, const char* key) {
    return vamp_kv_get(store, key) != NULL;
}

/** @brief Eliminar un par por clave */
bool vamp_kv_remove(vamp_key_value_store_t* store, const char* key) {
    if (!store || !key) return false;
    
    for (uint8_t i = 0; i < store->count; i++) {
        if (strcmp(store->pairs[i].key, key) == 0) {
            // Mover los elementos siguientes una posición hacia atrás
            for (uint8_t j = i; j < store->count - 1; j++) {
                memcpy(&store->pairs[j], &store->pairs[j + 1], sizeof(vamp_key_value_pair_t));
            }
            store->count--;
            // Limpiar el último elemento
            memset(&store->pairs[store->count], 0, sizeof(vamp_key_value_pair_t));
            return true;
        }
    }
    return false; // No encontrado
}

/** @brief Limpiar todos los pares */
void vamp_kv_clear(vamp_key_value_store_t* store) {
    if (!store) return;
    if (store->pairs) {
        free(store->pairs);
        store->pairs = NULL;
    }
    store->count = 0;
    store->capacity = 0;
}

/** @brief Convertir store a string para HTTP headers */
size_t vamp_kv_to_http_headers(const vamp_key_value_store_t* store, char* buffer, size_t buffer_size) {
    if (!store || !buffer || buffer_size == 0) return 0;
    
    size_t pos = 0;
    for (uint8_t i = 0; i < store->count; i++) {
        int written = snprintf(buffer + pos, buffer_size - pos, "%s: %s\r\n", 
                              store->pairs[i].key, store->pairs[i].value);
        if (written < 0 || (size_t)written >= (buffer_size - pos)) {
            break; // Buffer lleno
        }
        pos += written;
    }
    
    return pos;
}

/** @brief Convertir store a string para query parameters */
size_t vamp_kv_to_query_string(const vamp_key_value_store_t* store, char* buffer, size_t buffer_size) {
    if (!store || !buffer || buffer_size == 0) return 0;
    
    size_t pos = 0;
    for (uint8_t i = 0; i < store->count; i++) {
        const char* separator = (i == 0) ? "" : "&";
        int written = snprintf(buffer + pos, buffer_size - pos, "%s%s=%s", 
                              separator, store->pairs[i].key, store->pairs[i].value);
        if (written < 0 || (size_t)written >= (buffer_size - pos)) {
            break; // Buffer lleno
        }
        pos += written;
    }
    
    return pos;
}

