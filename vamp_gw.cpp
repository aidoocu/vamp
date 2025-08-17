/** @brief VAMP Gateway
 * 
 * 
 * 
 * 
 */
#include "vamp_gw.h"
#include "vamp_callbacks.h"
#include <cstring>
#include <cstdlib>

// Tabla unificada VAMP (NAT + Device + Session)
static vamp_entry_t vamp_table[VAMP_MAX_DEVICES];

/* Fecha de la última actualización de la tabla en UTC */
static char last_table_update[] = VAMP_TABLE_INIT_TSMP;

/* PATH del recurso VREG */
static char vamp_vreg_resource[VAMP_ENDPOINT_MAX_LEN];
/* ID del gateway */
static char vamp_gw_id[VAMP_GW_ID_MAX_LEN];

/* Buffer para la solicitud y respuesta de internet */
static char req_resp_internet_buff[VAMP_IFACE_BUFF_SIZE];

// Contadores globales
//static uint8_t vamp_device_count = 0;


// ======================== FUNCIONES VAMP TABLES ========================

void vamp_gw_vreg_init(char * vreg_url, char * gw_id){


	/* Verificar que los parámetros son válidos */
	if (vreg_url == NULL || gw_id == NULL || 
	    strlen(vreg_url) >= VAMP_ENDPOINT_MAX_LEN || 
	    strlen(gw_id) >= VAMP_GW_ID_MAX_LEN) {

		/* Limpiar recursos */
		vamp_vreg_resource[0] = '\0';
		vamp_gw_id[0] = '\0';
		
		return;
	}
	sprintf(vamp_vreg_resource, "%s", vreg_url);
	sprintf(vamp_gw_id, "%s", gw_id);
}


/** @todo CUANDO HAY UN ERROR ACTUALIZANDO LA TABLA YA SE QUEDA AHI Y NO SIGUE
 * ASI QUE DESPUES DEL ERROR NO SE ACTUALIZA LA TABLA NI SE INCORPORAN NUEVAS ENTRADAS
 * !!1!!!!!
 */
// Inicializar todas las tablas VAMP con sincronización VREG
void vamp_table_update() {

	/* Verificar que los valores de los recursos no estén vacíos */
	if (vamp_vreg_resource[0] == '\0' || vamp_gw_id[0] == '\0') {
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
	/*	 Formar la cadena de sincronización como un comando
	  	"sync --gateway gateway_id --last_time last_update"*/
	snprintf(req_resp_internet_buff, sizeof(req_resp_internet_buff), 
		"%s %s %s %s %s", VAMP_GW_SYNC, VAMP_GATEWAY_ID, vamp_gw_id, VAMP_TIMESTAMP, last_table_update);

		#ifdef VAMP_DEBUG
		Serial.print("cmd: ");
		Serial.println(req_resp_internet_buff);
		#endif /* VAMP_DEBUG */

	/* Enviar request usando TELL y recibir respuesta */
	if (vamp_iface_comm(vamp_vreg_resource, req_resp_internet_buff, strlen(req_resp_internet_buff))) {

		/* Primero hay que revisar se la respuesta es válida, mirando si contiene el prefijo esperado */
		/* sync */
		char * sync_resp = req_resp_internet_buff;

		if (!strstr(sync_resp, VAMP_GW_SYNC)){
			#ifdef VAMP_DEBUG
			Serial.println("No se recibió respuesta válida del VREG");
			#endif /* VAMP_DEBUG */
			return;
		}
		/* sync --updated */
		/* Si la tabla ya está actualizada, no hay nada más que hacer */
		if (strstr(sync_resp, VAMP_UPDATED)){
			#ifdef VAMP_DEBUG
			Serial.println("Tabla VAMP ya está actualizada, no hay nuevos datos");
			#endif /* VAMP_DEBUG */
			return;
		}
		/* sync --error <error> */
		if (strstr(sync_resp, VAMP_ERROR)){
			#ifdef VAMP_DEBUG
			Serial.print("Error en la sincronización VREG: ");
			Serial.println(sync_resp);
			#endif /* VAMP_DEBUG */
			return;
		}

		/* sync --timestamp <timestamp> --data <csv_data> */

		/* --timestamp <timestamp> */
		sync_resp = strstr(sync_resp, VAMP_TIMESTAMP);
		if (!sync_resp) {
			#ifdef VAMP_DEBUG
			Serial.println("No se encontró timestamp en la respuesta VREG");
			#endif /* VAMP_DEBUG */
			return;
		}

		/* !!!! Aqui hay un problema y es que si falla los datos mas abajo, ya no se puede recuperar el timestamp
		y queda actualizado, hay que tener cuidado con eso !!!!! */
		/* Extraer el timestamp de la respuesta */
		sync_resp = sync_resp + strlen(VAMP_TIMESTAMP) + 1; // +1 para saltar el espacio después del prefijo
		if (!vamp_get_timestamp(sync_resp)) {
			#ifdef VAMP_DEBUG
			Serial.println("Error extrayendo timestamp de la respuesta VREG");
			#endif /* VAMP_DEBUG */
			return;
		}

		/* Ahora deberia venir un campo con la cantidad de lineas */
		//char* line_count_str = strstr(req_resp_internet_buff, VAMP_DEV_COUNT);
		//if (line_count_str) {
		//	line_count_str += strlen(VAMP_DEV_COUNT) + 1; // Saltar el prefijo
		//	uint8_t line_count = atoi(line_count_str);
		//	Serial.print("Cantidad de líneas en la respuesta: ");
		//	Serial.println(line_count);
		//}

		/* Procesar la respuesta CSV (--data <csv_data>) */
		sync_resp = strstr(req_resp_internet_buff, VAMP_DATA);
		if (sync_resp) {

			/* Mover el puntero al inicio de los datos CSV,
			+1 para saltar el espacio después del prefijo */
			sync_resp = sync_resp + strlen(VAMP_DATA);

			/* Despues de --data'\n<csv_data> para enfatizar que son datos CSV */
			if (*sync_resp != '\n') {
				#ifdef VAMP_DEBUG
				Serial.println("Error: No se encontró valor de --data en la respuesta VREG");
				#endif /* VAMP_DEBUG */
				return;
			}

			sync_resp++;

			/* Esto se puede hacer mejor, mas elegante y manejar de paso el posible error */
			#ifdef VAMP_DEBUG
			Serial.print("Datos CSV recibidos: '\n'");
			Serial.println(sync_resp);

			/* Extraer los datos CSV de la respuesta */
			if (vamp_process_sync_response(sync_resp)) {
				Serial.println("Sincronización VREG completada exitosamente");
			} else {
				Serial.println("Error procesando respuesta VREG");
			}
			#endif /* VAMP_DEBUG */

			#ifndef VAMP_DEBUG
			vamp_process_sync_response(sync_resp);
			#endif /* VAMP_DEBUG */

			return;
		}

		/** @todo ESTO HAY QUE REFACTORIZANRLO */
		/* Si llegamos aquí, la respuesta no es válida */
		#ifdef VAMP_DEBUG
		Serial.println("no --data en respuesta VREG");
		#endif /* VAMP_DEBUG */

	} else {
		#ifdef VAMP_DEBUG
		Serial.println("Error comunicándose con servidor VREG");
		#endif /* VAMP_DEBUG */
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
	if (vamp_iface_comm(vamp_vreg_resource, req_resp_internet_buff, sizeof(req_resp_internet_buff))) {

		/* Primero hay que revisar se la respuesta es válida, mirando si contiene el prefijo esperado */
		if (!strstr(req_resp_internet_buff, VAMP_GET_NODE)){
			#ifdef VAMP_DEBUG
			Serial.println("No se recibió respuesta válida del VREG");
			#endif /* VAMP_DEBUG */
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
		vamp_table[table_index].wsn_id = vamp_generate_id_byte(table_index);
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
 *          primero si el nodo ya estaba en la tabla.
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
	while (csv_ptr[0] != '\0' && csv_ptr != NULL) {

		/* Si viene de un ciclo anterior hay que saltar el '\n' */
		if (csv_ptr[0] == '\n') {
			csv_ptr++;
		}

		/* El primer campo son 10 bytes con la direccion en HEX
			que se traduciran a 5 bytes de RF_ID */
		uint8_t rf_id[5];
		char rf_id_hex[VAMP_ADDR_LEN * 2 + 1]; // 10 caracteres + '\0'
		memcpy(rf_id_hex, csv_ptr, VAMP_ADDR_LEN * 2);
		rf_id_hex[VAMP_ADDR_LEN * 2] = '\0'; // Asegurar terminación

		#ifdef VAMP_DEBUG
		Serial.print("RF_ID recibido: ");
		Serial.println(rf_id_hex);
		#endif /* VAMP_DEBUG */

		/* convertir a RF_ID */
		if(!hex_to_rf_id(rf_id_hex, rf_id)) {
			#ifdef VAMP_DEBUG
			Serial.println("RF_ID inválido en la respuesta VREG");
			#endif /* VAMP_DEBUG */
			return false;
		}
		
		csv_ptr = csv_ptr + (VAMP_ADDR_LEN * 2 + 1); // Saltar el campo rf_id y la coma

		/* Si el ACTION es ADD */
		if (!strncmp(csv_ptr, "ADD", 3)){

			/* Buscar slot libre y asignar para luego configurar sus campos */
			uint8_t table_index = vamp_add_device(rf_id);

			/* Si se encontró un slot libre y se pudo adicionar el dispositivo se asigna 
			el estado de cache */
			if (table_index < VAMP_MAX_DEVICES) {

				/* Asignar el estado de cache */
				vamp_table[table_index].status = VAMP_DEV_STATUS_CACHE;

				/* Saltar "ADD," */
				csv_ptr += 4;

				/* Poner el type que puede ser '0' - '3' (0x30 - 0x33)
				(fijo, dinámico, auto, huérfano)*/
				if( 0x30 <= csv_ptr[0] && csv_ptr[0] <= 0x33) { 
					/* Si es un número, convertir ASCII a entero */
					vamp_table[table_index].type = ((uint8_t)csv_ptr[0] - 0x30);
				}
				else {
					#ifdef VAMP_DEBUG
					Serial.println("Tipo inválido en la respuesta VREG");
					#endif /* VAMP_DEBUG */
					return false; // Error en el tipo
				}

				/* Saltar el tipo y la coma, e. g "2,", */
				csv_ptr += 2;

				/* 	A partir de aqui vendrán los profiles, que pueden estar entre 0 y 
					VAMP_MAX_PROFILES, estos vendran en orden hasta el final de la linea
					con la forma:
					<protocol>,<method>,<endpoint_resource>,<protocol_params> */

				/* Inicializar el contador de perfiles */
				uint8_t profile_index = 0;
				vamp_table[table_index].profile_count = 0;

				/* Mientras que no se llegue al final de la linea o del archivo */
				char * find_comma = NULL;
				while (csv_ptr && (csv_ptr[0] != '\n') && (csv_ptr[0] != '\0')) {

					//vamp_table[table_index].profiles[profile_index].protocol = atoi(csv_ptr);
					//csv_ptr = strchr(csv_ptr, ',');
					//if (!csv_ptr) break;
					//csv_ptr++;

					/* Método específico del protocolo */
					vamp_table[table_index].profiles[profile_index].method = atoi(csv_ptr);
					csv_ptr = strchr(csv_ptr, ',');
					if (!csv_ptr) break;
					csv_ptr++;

					/* Endpoint resource */
					find_comma = strchr(csv_ptr, ',');
					if (!find_comma) { break; }
					uint8_t field_len = (uint8_t)(find_comma - csv_ptr);
					if (field_len >= VAMP_ENDPOINT_MAX_LEN) {
						#ifdef VAMP_DEBUG
						Serial.println("Endpoint resource demasiado largo en la respuesta VREG");
						#endif /* VAMP_DEBUG */
						break;
					}
					/* Liberar previo si existiera y asignar nuevo */
					if (vamp_table[table_index].profiles[profile_index].endpoint_resource) {
						free(vamp_table[table_index].profiles[profile_index].endpoint_resource);
						vamp_table[table_index].profiles[profile_index].endpoint_resource = NULL;
					}
					vamp_table[table_index].profiles[profile_index].endpoint_resource = (char*)malloc(field_len + 1);
					if (!vamp_table[table_index].profiles[profile_index].endpoint_resource) { break; }
					memcpy(vamp_table[table_index].profiles[profile_index].endpoint_resource, csv_ptr, field_len);
					vamp_table[table_index].profiles[profile_index].endpoint_resource[field_len] = '\0';
					csv_ptr = find_comma + 1; // Mover al siguiente campo

					find_comma = strchr(csv_ptr, ',');
					if (!find_comma) { break; }
					field_len = (uint8_t)(find_comma - csv_ptr);
					if (field_len >= VAMP_PROTOCOL_PARAMS_MAX_LEN) {
						#ifdef VAMP_DEBUG
						Serial.println("Protocol params demasiado largo en la respuesta VREG");
						#endif /* VAMP_DEBUG */
						break;
					}
					/* Liberar previo si existiera y asignar nuevo */
					if (vamp_table[table_index].profiles[profile_index].protocol_params) {
						free(vamp_table[table_index].profiles[profile_index].protocol_params);
						vamp_table[table_index].profiles[profile_index].protocol_params = NULL;
					}
					vamp_table[table_index].profiles[profile_index].protocol_params = (char*)malloc(field_len + 1);
					if (!vamp_table[table_index].profiles[profile_index].protocol_params) { break; }
					memcpy(vamp_table[table_index].profiles[profile_index].protocol_params, csv_ptr, field_len);
					vamp_table[table_index].profiles[profile_index].protocol_params[field_len] = '\0';
					csv_ptr = find_comma + 1; // Mover al siguiente campo

					profile_index++;
					vamp_table[table_index].profile_count = profile_index;
					/** @todo este error hay que manejarlo mejor */
					if (profile_index >= VAMP_MAX_PROFILES) break;

				}

			} else {
				/* Si ya no hay más slots libre pues nada que hacer */
				#ifdef VAMP_DEBUG
				Serial.println("No hay slots libres para agregar el dispositivo");
				#endif /* VAMP_DEBUG */
				return false;
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
				
				/* Limpiar perfiles anteriores y reiniciar contador */
				vamp_clear_device_profiles(table_index);
				vamp_table[table_index].profile_count = 0;

				/* Parsear perfiles como en ADD */
				uint8_t profile_index = 0;
				char * find_comma = NULL;
				while (csv_ptr && (csv_ptr[0] != '\n') && (csv_ptr[0] != '\0')) {
					/* Método */
					vamp_table[table_index].profiles[profile_index].method = atoi(csv_ptr);
					csv_ptr = strchr(csv_ptr, ',');
					if (!csv_ptr) break;
					csv_ptr++;

					/* Endpoint resource */
					find_comma = strchr(csv_ptr, ',');
					if (!find_comma) break;
					uint8_t field_len = (uint8_t)(find_comma - csv_ptr);
					if (field_len >= VAMP_ENDPOINT_MAX_LEN) { break; }
					if (vamp_table[table_index].profiles[profile_index].endpoint_resource) {
						free(vamp_table[table_index].profiles[profile_index].endpoint_resource);
						vamp_table[table_index].profiles[profile_index].endpoint_resource = NULL;
					}
					vamp_table[table_index].profiles[profile_index].endpoint_resource = (char*)malloc(field_len + 1);
					if (!vamp_table[table_index].profiles[profile_index].endpoint_resource) break;
					memcpy(vamp_table[table_index].profiles[profile_index].endpoint_resource, csv_ptr, field_len);
					vamp_table[table_index].profiles[profile_index].endpoint_resource[field_len] = '\0';
					csv_ptr = find_comma + 1;

					/* Protocol params */
					find_comma = strchr(csv_ptr, ',');
					if (!find_comma) break;
					field_len = (uint8_t)(find_comma - csv_ptr);
					if (field_len >= VAMP_PROTOCOL_PARAMS_MAX_LEN) { break; }
					if (vamp_table[table_index].profiles[profile_index].protocol_params) {
						free(vamp_table[table_index].profiles[profile_index].protocol_params);
						vamp_table[table_index].profiles[profile_index].protocol_params = NULL;
					}
					vamp_table[table_index].profiles[profile_index].protocol_params = (char*)malloc(field_len + 1);
					if (!vamp_table[table_index].profiles[profile_index].protocol_params) break;
					memcpy(vamp_table[table_index].profiles[profile_index].protocol_params, csv_ptr, field_len);
					vamp_table[table_index].profiles[profile_index].protocol_params[field_len] = '\0';
					csv_ptr = find_comma + 1;

					profile_index++;
					vamp_table[table_index].profile_count = profile_index;
					if (profile_index >= VAMP_MAX_PROFILES) break;
				}

				/* Avanzar al final de la línea */
				csv_ptr = strchr(csv_ptr, '\n');
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


bool vamp_gw_wsn(void) {

    /* Extraer el mensaje de la interface via callback */
	uint8_t data_recv = vamp_wsn_comm(wsn_buffer, VAMP_MAX_PAYLOAD_SIZE);

	if (data_recv == 0) {
		return false; // No hay datos disponibles
	}

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

		/*  En el segundo byte se encuentra el ID del dispositivo
		 	en la forma de: Formato del byte ID: [VVV][IIIII] */
		vamp_entry_t * entry = &vamp_table[VAMP_GET_INDEX(wsn_buffer[1])];
		/* Verificar que la verificación es correcta */
		if (entry->wsn_id != wsn_buffer[1]) {
			#ifdef VAMP_DEBUG
			Serial.println("Verificación de dispositivo fallida");
			#endif /* VAMP_DEBUG */
			return false; // Verificación fallida
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

		/* Como la respuesta del servidor puede demorar y 
		probablemente el mote no resuelva nada con ella
		le respondemos y ack para que el mote sepa que 
		al menos su tarea fue recibida */
		uint8_t ack_buffer[1] = { VAMP_ACK | VAMP_IS_CMD_MASK };
		vamp_wsn_comm(entry->rf_id, ack_buffer, 1);

		/* Copiar los datos recibidos al buffer de internet si es que hay datos */
		if(rec_len > 0) {
			memcpy(req_resp_internet_buff, &wsn_buffer[data_offset], rec_len);
		}
		req_resp_internet_buff[rec_len] = '\0'; // Asegurar terminación de cadena si es necesario

		/* Enviar con el perfil completo (método/endpoint/params) */
		if (profile->endpoint_resource && profile->endpoint_resource[0] != '\0') {
			vamp_iface_comm(profile, req_resp_internet_buff, rec_len);
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
    if (vamp_table[device_index].profiles[profile_index].protocol_params) {
        free(vamp_table[device_index].profiles[profile_index].protocol_params);
        vamp_table[device_index].profiles[profile_index].protocol_params = NULL;
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
    
    // Copiar protocol_params si no es NULL
    if (profile->protocol_params) {
        size_t len = strlen(profile->protocol_params);
        vamp_table[device_index].profiles[profile_index].protocol_params = (char*)malloc(len + 1);
        if (vamp_table[device_index].profiles[profile_index].protocol_params) {
            strcpy(vamp_table[device_index].profiles[profile_index].protocol_params, profile->protocol_params);
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
        if (vamp_table[device_index].profiles[i].protocol_params) {
            free(vamp_table[device_index].profiles[i].protocol_params);
            vamp_table[device_index].profiles[i].protocol_params = NULL;
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

