/** @brief VAMP Gateway
 * 
 * 
 * 
 * 
 */
#include "vamp_gw.h"

// Tabla unificada VAMP (NAT + Device + Session)
static vamp_entry_t vamp_table[VAMP_MAX_DEVICES];

/* Fecha de la última actualización de la tabla en UTC */
static char last_table_update[] = VAMP_TABLE_INIT_TSMP;

/* PATH del recurso VREG */
static char vamp_vreg_resource[VAMP_ENDPOINT_MAX_LEN];
/* ID del gateway */
static char vamp_gw_id[VAMP_GW_ID_MAX_LEN];

/* Buffer para la solicitud y respuesta de internet */
static char req_resp_internet_buff[1024];

// Contadores globales
//static uint8_t vamp_device_count = 0;

/* ========================== Callbacks =========================== */

/* Callback para comunicación http */

static vamp_internet_callback_t internet_comm_callback = NULL;
static vamp_wsn_callback_t wsn_comm_callback = NULL;


/* ======================== Inicialización ======================== */

/** @brief Initialize VAMP Gateway module */
void vamp_local_gw_init(vamp_internet_callback_t internet_callback, vamp_wsn_callback_t wsn_callback, const char * vamp_vreg, const char * vamp_gw) {

	/* Verificar que los parámetros son válidos */
	if (vamp_vreg == NULL || vamp_gw == NULL || 
	    strlen(vamp_vreg) >= VAMP_ENDPOINT_MAX_LEN || 
	    strlen(vamp_gw) >= VAMP_GW_ID_MAX_LEN) {

		vamp_vreg_resource[0] = '\0'; // Limpiar recurso
		vamp_gw_id[0] = '\0'; // Limpiar ID de gateway
		
		Serial.println("Parámetros no válidos");

		return;
	}
	sprintf(vamp_vreg_resource, "%s", vamp_vreg);
	sprintf(vamp_gw_id, "%s", vamp_gw);

	internet_comm_callback = internet_callback;
	wsn_comm_callback = wsn_callback;
	Serial.println("Callbacks comm registrados");

	return;
}


// ======================== FUNCIONES VAMP TABLES ========================


// Inicializar todas las tablas VAMP con sincronización VREG
void vamp_table_update() {

	// Verificar si los parámetros son válidos
	if (vamp_vreg_resource == NULL || vamp_gw_id == NULL) {
		Serial.println("Parámetros no válidos");
		return;
	}

	// Ver cuando se actualizó la tabla por última vez
	if(!strcmp(last_table_update, VAMP_TABLE_INIT_TSMP)) {
		// La tabla no ha sido inicializada
		Serial.println("Tabla VAMP no inicializada, inicializando...");

		// Inicializar tabla unificada VAMP
		// Solo necesitamos verificar/setear el status, el resto se inicializa cuando se asigna
		for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
			vamp_table[i].status = VAMP_DEV_STATUS_FREE;
		}
		
		// Resetear contadores
		//vamp_device_count = 0;
		
		Serial.println("Tabla VAMP inicializada localmente");
	}
    
	Serial.println("Sincronizando tabla VAMP con servidor VREG...");
	
	// formamos la cadena de sincronización como un comando
	// el formato es: "sync --gateway gateway_id --last_time last_update"
	snprintf(req_resp_internet_buff, sizeof(req_resp_internet_buff), 
		"%s %s %s %s %s", VAMP_GW_SYNC, VAMP_GATEWAY_ID, vamp_gw_id, VAMP_TIMESTAMP, last_table_update);

	// Enviar request usando TELL y recibir respuesta
	if (internet_comm_callback(vamp_vreg_resource, VAMP_TELL, req_resp_internet_buff, sizeof(req_resp_internet_buff))) {

		/* Primero hay que revisar se la respuesta es válida, mirando si contiene el prefijo esperado */
		/* sync */
		char * sync_resp = req_resp_internet_buff;

		if (!strstr(sync_resp, VAMP_GW_SYNC)){
			Serial.println("No se recibió respuesta válida del VREG");
			return;
		}
		/* sync --updated */
		/* Si la tabla ya está actualizada, no hay nada más que hacer */
		if (strstr(sync_resp, VAMP_UPDATED)){
			Serial.println("Tabla VAMP ya está actualizada, no hay nuevos datos");
			return;
		}
		/* sync --error <error> */
		if (strstr(sync_resp, VAMP_ERROR)){
			Serial.printf("Error en la sincronización VREG: %s\n", sync_resp);
			return;
		}

		
		/* Si llegamos aquí, la respuesta es válida y contiene datos CSV */
		Serial.println("Respuesta VREG válida, procesando datos CSV...");

		/* sync --timestamp <timestamp> --data <csv_data> */

		/* --timestamp <timestamp> */
		char* timestamp_start = strstr(sync_resp, VAMP_TIMESTAMP);
		if (!timestamp_start) {
			Serial.printf("Error: No se encontró timestamp en la respuesta VREG: %s\n", sync_resp);
			return;
		}

		/* Mover el puntero al inicio del valor del timestamp (después de "--timestamp ") */
		timestamp_start += strlen(VAMP_TIMESTAMP) + 1; // +1 para saltar el espacio

		/* Encontrar el final del timestamp (siguiente espacio o final de línea) */
		char* timestamp_end = strchr(timestamp_start, ' ');
		if (!timestamp_end) {
			timestamp_end = strchr(timestamp_start, '\n');
			if (!timestamp_end) {
				timestamp_end = timestamp_start + strlen(timestamp_start);
			}
		}

		/* Calcular la longitud del timestamp y validar */
		size_t timestamp_len = timestamp_end - timestamp_start;
		size_t max_timestamp_len = sizeof(last_table_update) - 1;
		
		if (timestamp_len > max_timestamp_len) {
			Serial.printf("Error: Timestamp demasiado largo (%zu > %zu)\n", timestamp_len, max_timestamp_len);
			return;
		}

		/* Extraer el timestamp de forma segura */
		memcpy(last_table_update, timestamp_start, timestamp_len);
		last_table_update[timestamp_len] = '\0'; // Asegurar el null-terminator
		//vamp_validate_timestamp(last_table_update); OJO HACER!!!!!
		Serial.printf("Tabla VAMP sincronizada, última actualización: %s\n", last_table_update);

		/* Ahora deberia venir un campo con la cantidad de lineas */
		//char* line_count_str = strstr(req_resp_internet_buff, VAMP_DEV_COUNT);
		//if (line_count_str) {
		//	line_count_str += strlen(VAMP_DEV_COUNT) + 1; // Saltar el prefijo
		//	uint8_t line_count = atoi(line_count_str);
		//	Serial.printf("Cantidad de líneas en la respuesta: %d\n", line_count);
		//}

		/* Procesar la respuesta CSV (--data <csv_data>) */
		sync_resp = strstr(req_resp_internet_buff, VAMP_DATA);
		if (sync_resp) {

			/* Mover el puntero al inicio de los datos CSV,
			+1 para saltar el espacio después del prefijo */
			sync_resp = sync_resp + strlen(VAMP_DATA) + 1;

			/* Extraer los datos CSV de la respuesta */
			if (vamp_process_sync_response(sync_resp)) {
				Serial.println("Sincronización VREG completada exitosamente");
			} else {
				Serial.println("Error procesando respuesta VREG");
			}
			return;
		}

		/* Si llegamos aquí, la respuesta no es válida */
		Serial.println("no --data en respuesta VREG");

	} else {
		Serial.println("Error comunicándose con servidor VREG");
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

	Serial.printf("Enviando %s\n", req_resp_internet_buff);

	// Enviar request usando TELL y recibir respuesta
	if (internet_comm_callback(vamp_vreg_resource, VAMP_TELL, req_resp_internet_buff, sizeof(req_resp_internet_buff))) {

		/* Primero hay que revisar se la respuesta es válida, mirando si contiene el prefijo esperado */
		if (!strstr(req_resp_internet_buff, VAMP_GET_NODE)){
			Serial.println("No se recibió respuesta válida del VREG");
			return VAMP_MAX_DEVICES;
		}

		/* Si la respuesta contiene un error, mostrarlo */
		char* csv_data = strstr(req_resp_internet_buff, VAMP_GW_SYNC);
		if (csv_data) {

			/* Mover el puntero al inicio de los datos CSV,
			+1 para saltar el espacio después del prefijo */
			csv_data = csv_data + strlen(VAMP_GW_SYNC) + 1;

			/* Extraer los datos CSV de la respuesta, aqui deberia venir unos solo */
			if (vamp_process_sync_response(csv_data)) {
				Serial.println("Sincronización VREG completada exitosamente");
				/* Buscar el dispositivo en la tabla */
				return (vamp_find_device(rf_id));
			}
		}
	}
	Serial.println("Error procesando respuesta VREG");
	return VAMP_MAX_DEVICES; // No se espera respuesta de datos
}













// ======================== FUNCIONES ID COMPACTO ========================

/* Generar byte de ID compacto (verification + index) para un RF_ID */
uint8_t vamp_generate_id_byte(const uint8_t table_index) {

	/** Generar verificación del puerto a partir del numero que ya este
		en el slot del indice para evitar que sea el mismo */
	uint8_t check_bits = VAMP_GET_VERIFICATION(vamp_table[table_index].port);
	check_bits++; // Aumentar para evitar que sea el mismo

	// Generar ID byte: 3 bits de verificación + 5 bits de índice
	return (VAMP_MAKE_ID_BYTE(check_bits, table_index));
}



/** ????? @todo hay que definir algun elemento que no sea correcto como una direccion nula
 00:00:00:00:00 o de broadcast o ... */
bool vamp_is_rf_id_valid(const uint8_t * rf_id) {
	// Verificar que el RF_ID no sea NULL y tenga la longitud correcta
	if (rf_id == NULL) {
		return false;
	}
	
	if (rf_id[0] == 0 && rf_id[1] == 0 && rf_id[2] == 0 &&
	    rf_id[3] == 0 && rf_id[4] == 0) {
		return false; // RF_ID nulo
	}

	if (rf_id[0] == 0xFF && rf_id[1] == 0xFF && rf_id[2] == 0xFF &&
	    rf_id[3] == 0xFF && rf_id[4] == 0xFF) {
		return false; // RF_ID de broadcast
	}

	return true;
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
    // Solo marcar como libre - otros campos se sobrescriben cuando se reasigna
    vamp_table[index].status = VAMP_DEV_STATUS_FREE;
  }
}

/* Agregar dispositivo a la tabla */
uint8_t vamp_add_device(const uint8_t* rf_id) {

	/* Asegurarse que el dispositivo es válido */
	if (!vamp_is_rf_id_valid(rf_id)) {
		Serial.println("RF_ID no válido");
		return VAMP_MAX_DEVICES;
	}

	/* Verificar si el dispositivo ya está en la tabla */
	uint8_t table_index = vamp_find_device(rf_id);
	if (table_index < VAMP_MAX_DEVICES) {
		/* Ya existe, no hacer nada */
		return table_index;
	}

	/* buscar un slot libre */
	for (table_index = 0; table_index < VAMP_MAX_DEVICES; table_index++) {
		if (vamp_table[table_index].status == VAMP_DEV_STATUS_FREE) {
			break;
		}
	}

	/* Si no hubiera slots libres, buscamos el sensor inactivo mas antiguo */
	if(table_index == VAMP_MAX_DEVICES) {
		table_index = vamp_get_oldest_inactive();
	}

	/* Si encontramos un slot libre o inactivo */
	if (table_index < VAMP_MAX_DEVICES) {
		// Reemplazar el sensor inactivo más antiguo
		vamp_clear_entry(table_index);
		memcpy(vamp_table[table_index].rf_id, rf_id, VAMP_ADDR_LEN);
		vamp_table[table_index].port = vamp_generate_id_byte(table_index);
		vamp_table[table_index].status = VAMP_DEV_STATUS_ADDED;
		vamp_table[table_index].last_activity = millis();
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
      // Verificar timeout de dispositivo
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


/** 
 * Process the synchronization response from VREG.
 * El buffer contiene la respuesta que debe tener el formato:
 * gateway_sync_resp <--option> <value>
 * <value> es el nuevo valor para la opción
 * Opciones:
 * - "--updated": indica que la tabla ya está actualizada, no hay nuevos datos, no tiene value
 * - "--error": indica que hubo un error en la sincronización, value contiene el mensaje de error
 * - "--data": contiene los datos de la tabla en formato CSV, value es el CSV con los campos:
 *  	rf_id, action, type, resource
 * actions:
 * - "ADD": agregar un dispositivo. El VREG no sabe ni debe intervenir en el puerto, 
 *          así que el gateway busca un slot vacío y asigna el nuevo nodo. Se revisa 
 *          primero si el nodo ya estaba en la tabla. Al adicionar un nodo nuevo se 
 *          cambia el prefijo de puerto de 8 a 7 para indicar que es un cache que 
 *          ningún nodo ha reclamado aún.
 * - "REMOVE": eliminar un dispositivo, se cambia el estado a libre y se pone a cero el rf_id
 * - "UPDATE": actualizar un dispositivo existente
*/
bool vamp_process_sync_response(const char* csv_data) {
	
	if (csv_data == NULL) {
		return false;
	}
  
	/* Apuntamos a la primera linea */
	const char * csv_ptr = csv_data; // VER probablemente csv_ptr no sea necesario

	/* 	Procesar cada línea, que tiene la forma: 
		rf_id,action,type,resource'\n' */
	while (csv_ptr != NULL) {

		/* Si viene de un ciclo anterior hay que saltar el '\n' */
		if (csv_ptr[0] == '\n') {
			csv_ptr++;
		}

		/* El primer campo son 10 bytes con la direccion en HEX
			que se traduciran a 5 bytes de RF_ID */
		uint8_t rf_id[5];	
		if(!hex_to_rf_id(csv_ptr, rf_id)) {
			Serial.println("RF_ID inválido en la respuesta VREG");
			return false;
		}
		
		csv_ptr += 11; // Saltar el campo rf_id y la coma

		/* Si el ACTION es ADD */
		if (!strncmp(csv_ptr, "ADD", 3)){

			// Buscar slot libre y asignar
			uint8_t table_index = vamp_add_device(rf_id);

			/* Si se encontró un slot libre se asigna el estado de cache */
			if (table_index < VAMP_MAX_DEVICES) {

				/* Asignar el estado de cache */
				vamp_table[table_index].status = VAMP_DEV_STATUS_CACHE;

				/* Saltar "ADD," */
				csv_ptr += 4;

				/* Poner el type */
				vamp_table[table_index].type = atoi(csv_ptr);
				
				/* Saltar el tipo y la coma, e. g "2,", */
				csv_ptr += 2; 
				/* y como ahora lo que viene es el resource hasta el final de la línea
				hay que buscar ese final que puede ser un '\n' o final del buffer 
				(csv_ptr = NULL) */
				char* end_ptr = strchr(csv_ptr, '\n');

				if (end_ptr) {

					/* Si hay un final de línea, calcular la longitud del resource */
					uint8_t resource_len = end_ptr - csv_ptr;
					if (resource_len >= VAMP_ENDPOINT_MAX_LEN) {
						Serial.println("Resource demasiado largo en la respuesta VREG");
						return false; //este error no debería pasar, pero por si acaso hay que manejarlo mejor que esto
					}

					/* Poner el resource */

					strncpy(vamp_table[table_index].endpoint_resource, csv_ptr, resource_len);
					vamp_table[table_index].endpoint_resource[resource_len] = '\0'; // Asegurar terminación
					
					csv_ptr = end_ptr; // Mover el puntero al final de la línea
				} /* Si fuera NULL el while lo vera arriba */

			} else {
				/* Buscar el final de la línea o del buffer (csv_ptr = NULL) */
				csv_ptr = strchr(csv_ptr, '\n');
			}

		/* Si el ACTION es REMOVE */	
		} else if (!strncmp(csv_ptr, "REMOVE", 6)) {

			vamp_remove_device(rf_id);

			/* Buscar el final de la línea o del buffer (csv_ptr = NULL) */
			csv_ptr = strchr(csv_ptr, '\n');

		/* Si el ACTION es UPDATE */	
		} else if (!strncmp(csv_ptr, "UPDATE", 6)) {

			/* Buscar el dispositivo en la tabla */
			uint8_t table_index = vamp_find_device(rf_id);

			if (table_index < VAMP_MAX_DEVICES) {
				/* Si se encontró el dispositivo en la tabla, actualizarlo */
				csv_ptr += 7; // Saltar "UPDATE,"
				
				/* Poner el tipo */
				vamp_table[table_index].type = atoi(csv_ptr);
				
				/* Saltar el tipo y la coma, e. g "2," */
				csv_ptr += 2; 
				
				/* Buscar el final de la línea o del buffer */
				char* end_ptr = strchr(csv_ptr, '\n');
				if (!end_ptr) {
					end_ptr = strchr(csv_ptr, '\0'); // Buscar el final del buffer
				}
				
				uint8_t resource_len = end_ptr - csv_ptr;
				if (resource_len >= VAMP_ENDPOINT_MAX_LEN) {
					Serial.println("Resource demasiado largo en la respuesta VREG");
					return false; //este error no debería pasar, pero por si acaso hay que manejarlo mejor que esto
				}

				/* Poner el resource */
				strncpy(vamp_table[table_index].endpoint_resource, csv_ptr, resource_len);
				vamp_table[table_index].endpoint_resource[resource_len] = '\0'; // Asegurar terminación
				
				csv_ptr = end_ptr; // Mover el puntero al final de la línea
			} else {
				/* Buscar el final de la línea o del buffer (csv_ptr = NULL) */
				csv_ptr = strchr(csv_ptr, '\n');
			}
		}
	}
  return true; // Procesamiento exitoso
}


/* --------------- WSN --------------- */

/* Buffer para datos WSN */
static uint8_t wsn_buffer[VAMP_MAX_PAYLOAD_SIZE];


bool vamp_get_wsn(void) {

    /* Extraer el mensaje de la interface via callback */
	uint8_t data_recv = wsn_comm_callback(NULL, VAMP_ASK, wsn_buffer, VAMP_MAX_PAYLOAD_SIZE);

	if (data_recv == 0) {
		Serial.println("No se recibieron datos WSN");
		return false; // No hay datos disponibles
	}

	/* Verificar si es de datos o de comando */
	if (VAMP_WSN_IS_COMMAND(wsn_buffer)) {

		/* Aislar el comando */
		wsn_buffer[0] = VAMP_WSN_GET_CMD(wsn_buffer);
		uint8_t table_index = 0;
		
		/* Procesar el comando */
		switch (wsn_buffer[0]) {
			case VAMP_JOIN_REQ:
				/* Manejar el comando JOIN_REQ */
				Serial.println("Comando JOIN_REQ recibido");

				/* Verificamos que tenga el largo correcto */
				if (data_recv != VAMP_JOIN_REQ_LEN) {
					Serial.println("JOIN_REQ inválido, largo incorrecto");
					return false; // Comando inválido
				}
				table_index = vamp_find_device(&wsn_buffer[1]); // Asumimos que el RF_ID está después del comando

				if (table_index == VAMP_MAX_DEVICES) {
					
					Serial.println("No dev en cache");

					/* Si no esta en el cache hay que preguntarle al VREG */
					table_index = vamp_get_vreg_device(&wsn_buffer[1]);
					if (table_index == VAMP_MAX_DEVICES) {
						Serial.println("Error al obtener el dispositivo del VREG");
						return false; // Error al obtener el dispositivo
					}

					Serial.printf("Dispositivo encontrado en VREG, index: %d\n", table_index);

					/* Formamos la respuesta para el nodo solicitante */
					wsn_buffer[0] = VAMP_JOIN_ACK;
					wsn_buffer[1] = table_index;
					for (int i = 0; i < VAMP_ADDR_LEN; i++) {
						wsn_buffer[i + 2] = (uint8_t)vamp_gw_id[i]; // Asignar el ID del gateway
					}
					/* Reportamos al nodo solicitante */
					wsn_comm_callback(NULL, VAMP_TELL, wsn_buffer, 2 + VAMP_ADDR_LEN);

				}

				break;

			case VAMP_JOIN_ACK:
				Serial.println("Comando JOIN_ACK recibido");
				break;
			case VAMP_PING:
				/* Manejar el comando PING */
				Serial.println("Comando PING recibido");
				break;
			case VAMP_PONG:
				/* Manejar el comando PONG */
				Serial.println("Comando PONG recibido");
				break;
			default:
				/* Comando desconocido */
				Serial.printf("Comando desconocido: %02X\n", wsn_buffer[0]);
				break;
		}


	}

	return true; // Procesamiento exitoso
}

/* --------------------- Funciones públicas tabla VAMP -------------------- */

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

/** @brief Obtener estado legible de un dispositivo */
const char* vamp_get_status_string(uint8_t status) {
    switch (status) {
        case VAMP_DEV_STATUS_FREE:
            return "Free";
        case VAMP_DEV_STATUS_INACTIVE:
            return "Inactive";
        case VAMP_DEV_STATUS_ACTIVE:
            return "Active";
        case VAMP_DEV_STATUS_ADDED:
            return "Added";
        case VAMP_DEV_STATUS_CACHE:
            return "Cache";
        default:
            return "Unknown";
    }
}

/** @brief Obtener tipo legible de un dispositivo */
const char* vamp_get_type_string(uint8_t type) {
    switch (type) {
        case VAMP_DEV_TYPE_FIXED:
            return "Fixed";
        case VAMP_DEV_TYPE_DYNAMIC:
            return "Dynamic";
        case VAMP_DEV_TYPE_AUTO:
            return "Auto";
        default:
            return "Unknown";
    }
}
