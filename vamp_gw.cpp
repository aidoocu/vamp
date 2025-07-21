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
static char last_table_update[] = {VAMP_TABLE_INIT_TSMP};

/* URL del servidor VREG */
static char vamp_vreg_resource[VAMP_ENDPOINT_MAX_LEN];
/* ID del gateway */
static char vamp_gw_id[VAMP_GW_ID_MAX_LEN];

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
	
	// Generar request de sincronización, usar el mismo buffer para request y response
	char request_response_buffer[1024];

	// formamos la cadena de sincronización como un comando
	// el formato es: "VAMP_SYNC --gateway gateway_id --last_time last_update"
	snprintf(request_response_buffer, sizeof(request_response_buffer), 
		"%s --gateway %s --last_time %s", VAMP_GW_SYNC_REQ, vamp_gw_id, last_table_update);

	// Enviar request usando TELL y recibir respuesta
	if (internet_comm_callback(vamp_vreg_resource, VAMP_TELL, request_response_buffer, sizeof(request_response_buffer))) {

		/* Primero hay que revisar se la respuesta es válida, mirando si contiene el prefijo esperado */
		if (!strstr(request_response_buffer, VAMP_VREG_SYNC_RESP)){
			Serial.println("No se recibió respuesta válida del VREG");
			return;
		}
		/* Si la tabla ya está actualizada, no hay nada más que hacer */
		if (strstr(request_response_buffer, VAMP_VREG_SYNC_UPDATED)){
			Serial.println("Tabla VAMP ya está actualizada, no hay nuevos datos");
			return;
		}

		if (strstr(request_response_buffer, VAMP_VREG_SYNC_ERROR)){
			Serial.printf("Error en la sincronización VREG: %s\n", request_response_buffer);
			return;
		}

		// Procesar la respuesta CSV
		char* csv_data = strstr(request_response_buffer, VAMP_VREG_SYNC_OK);
		if (csv_data) {

			/* Mover el puntero al inicio de los datos CSV,
			+1 para saltar el espacio después del prefijo */
			csv_data = csv_data + strlen(VAMP_VREG_SYNC_OK) + 1;

			/* Extraer los datos CSV de la respuesta */
			if (vamp_process_sync_response(csv_data)) {
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
bool vamp_add_device(const uint8_t* rf_id) {

	/* Verificar si el dispositivo ya está en la tabla */
	uint8_t table_index = vamp_find_device(rf_id);
	if (table_index < VAMP_MAX_DEVICES) {
		/* Ya existe, no hacer nada */
		return true;
	}

	/* buscar un slot libre */
	for (table_index = 0; table_index < VAMP_MAX_DEVICES; table_index++) {
		if (vamp_table[table_index].status == VAMP_DEV_STATUS_FREE) {
			break;
		}

		/* Si no hubiera slots libres, buscamos el sensor inactivo mas antiguo */
		if( table_index == VAMP_MAX_DEVICES) {
			table_index = vamp_get_oldest_inactive();
		}

		if (table_index < VAMP_MAX_DEVICES) {
			// Reemplazar el sensor inactivo más antiguo
			vamp_clear_entry(table_index);
			memcpy(vamp_table[table_index].rf_id, rf_id, VAMP_ADDR_LEN);
			vamp_table[table_index].port = vamp_generate_id_byte(table_index);
			vamp_table[table_index].status = VAMP_DEV_STATUS_ADDED;
			vamp_table[table_index].last_activity = millis();
			return true;
		}
	}
  
  /* No hay slots libres ni inactivos */
  Serial.println("No hay slots libres para agregar dispositivo");
  return false;

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
	uint8_t oldest_index = 0; // Valor por defecto si no se encuentra

	for (oldest_index = 0; oldest_index < VAMP_MAX_DEVICES; oldest_index++) {
		if (vamp_table[oldest_index].status == VAMP_DEV_STATUS_INACTIVE) {
			if (vamp_table[oldest_index].last_activity < oldest_time) {
				oldest_time = vamp_table[oldest_index].last_activity;
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

		/* Si el ACTION es REMOVE */	
		} else if (!strncmp(csv_ptr, "REMOVE", 6)) {

			vamp_remove_device(rf_id);

			/* Buscar el final de la línea o del buffer (csv_ptr = NULL) */
			csv_ptr = strchr(csv_ptr, '\n');

		/* Si el ACTION es UPDATE */	
		} else if (strncmp(csv_ptr, "UPDATE", 6) == 0) {

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