/** 
 * 
 * 
 * 
 */

#include "vamp_table.h"
//#include "vamp_kv.h"
#include "../vamp_gw.h"
#include "../vamp_callbacks.h"

#ifdef ARDUINOJSON_AVAILABLE
#include "vamp_json.h"
#endif

/* Tabla global VAMP */
static vamp_entry_t vamp_table[VAMP_MAX_DEVICES];

/* Fecha de la última actualización de la tabla en UTC */
static char last_table_update[] = VAMP_TABLE_INIT_TSMP;

/* Timestamp en millis de la última sincronización (para calcular tiempo transcurrido) */
static uint32_t last_sync_millis = 0;

/* Sincronizar la tabla VAMP con VREG */
void vamp_table_update(vamp_profile_t * vreg_profile) {

	/* Verificar que los valores de los recursos no estén vacíos */
	if (vreg_profile->endpoint_resource[0] == '\0') {
		#ifdef VAMP_DEBUG
		printf("[VAMP] no gw resources defined\n");
		#endif /* VAMP_DEBUG */
		return;
	}

	/* Ver cuando se actualizó la tabla por última vez */
	if(!vamp_is_table_initialized()) {
		// La tabla no ha sido inicializada
		#ifdef VAMP_DEBUG
		printf("[VAMP] init vamp table\n");
		#endif /* VAMP_DEBUG */

		/* Inicializar la tabla VAMP */
		for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
			vamp_table[i].status = VAMP_DEV_STATUS_FREE;
		}
	}

	/* Configurar query_params con last_update */
	vamp_kv_clear(&vreg_profile->query_params);
	vamp_kv_set(&vreg_profile->query_params, "last_update", last_table_update);

	/* Enviar request usando TELL y recibir respuesta */
  #ifdef VAMP_DEBUG
  printf("[VAMP] sync vreg\n");
  #endif /* VAMP_DEBUG */

  if (vamp_iface_comm(vreg_profile, iface_buff, VAMP_IFACE_BUFF_SIZE)) {

		/* Extraer los datos JSON de la respuesta */
		#ifdef ARDUINOJSON_AVAILABLE

    printf("{MEM} memory status before table update\n");
		//printf("{TLS} free heap: %d B\n", ESP.getFreeHeap());
		printf("{MEM} frag: %d%%\n", ESP.getHeapFragmentation());
		printf("{MEM} ---- max block: %d B\n", ESP.getMaxFreeBlockSize());

    if (vamp_process_sync_json_response(iface_buff)) {
      #ifdef VAMP_DEBUG
			printf("[VAMP] VREG Sync successful\n");
      #endif /* VAMP_DEBUG */

      printf("{MEM} memory status after table update\n");
      //printf("{TLS} free heap: %d B\n", ESP.getFreeHeap());
      printf("{MEM} frag: %d%%\n", ESP.getHeapFragmentation());
      printf("{MEM} ---- max block: %d B\n", ESP.getMaxFreeBlockSize());

		}
    #ifdef VAMP_DEBUG
    else {
      
			printf("[VAMP] VREG Sync failed\n");
    #endif /* VAMP_DEBUG */
		}
		#endif /* ARDUINOJSON_AVAILABLE */

		return;
	}

	return;

}

/* Verificar si la tabla ha sido inicializada */
bool vamp_is_table_initialized(void) {
    return strcmp(last_table_update, VAMP_TABLE_INIT_TSMP) != 0;
}

/** Obtener timestamp de la última sincronización */
const char* vamp_get_last_sync_timestamp(void) {
    return last_table_update;
}

uint32_t vamp_get_last_sync_millis(void) {
    return last_sync_millis;
}

void vamp_set_last_sync_timestamp(const char * timestamp) {

    /** @todo Validar el formato del timestamp */
    /* Asegurarse que sea un timestamp válido */
    if (!timestamp || strlen(timestamp) != 20) {
        return;
    }

    strncpy(last_table_update, timestamp, sizeof(last_table_update) - 1);
    last_table_update[sizeof(last_table_update) - 1] = '\0'; // Asegurar terminación nula
    
    // Actualizar timestamp numérico
    last_sync_millis = millis();
}

/* --------------------- Manejo de nodos (entradas) -------------------- */

/* Obtener la cantidad de dispositivos en la tabla */
uint8_t vamp_get_dev_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < VAMP_MAX_DEVICES; i++) {
        if (vamp_table[i].status != VAMP_DEV_STATUS_FREE) {
            count++;
        }
    }
    return count;
}

/**
 * @brief Devuelve el número de dispositivos activos (status == VAMP_DEV_STATUS_ACTIVE)
 */
uint8_t vamp_get_active_dev_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < VAMP_MAX_DEVICES; i++) {
        if (vamp_table[i].status == VAMP_DEV_STATUS_ACTIVE) {
            count++;
        }
    }
    return count;
}

/* Obtener entrada de la tabla apuntada por el índice "index" */
vamp_entry_t * vamp_get_table_entry(uint8_t index) {
    if (index >= VAMP_MAX_DEVICES) {
        return NULL;
    }
    
    // Solo retornar entradas no libres
    if (vamp_table[index].status == VAMP_DEV_STATUS_FREE) {
        return NULL;
    }
    
    return &vamp_table[index];
}

/* Limpiar una entrada específica de la tabla */
void vamp_clear_entry(int index) {
  if (index < 0 || index >= VAMP_MAX_DEVICES) {
    return;
  }
  
  if (vamp_table[index].status != VAMP_DEV_STATUS_FREE) {
		// Liberar recursos asignados dinámicamente en todos los perfiles
		vamp_clear_device_profiles(index);
		
		// Limpiar contenido del buffer (NO liberar memoria pre-asignada)
		if (vamp_table[index].data_buff) {
			memset(vamp_table[index].data_buff, 0, VAMP_MAX_PAYLOAD_SIZE + 1);
		}
		
    // Solo marcar como libre - otros campos se sobrescriben cuando se reasigna
    vamp_table[index].status = VAMP_DEV_STATUS_FREE;
  }
}

/*  Agregar dispositivo con "rf_id" a la tabla sin inicializarlo. La entrada queda vacia. 
    devuelve el índice del dispositivo o VAMP_MAX_DEVICES si falla */
uint8_t vamp_add_device(const uint8_t* rf_id) {

	/* Asegurarse que el dispositivo es válido */
	if (!vamp_is_rf_id_valid(rf_id)) {
		#ifdef VAMP_DEBUG
		printf("[TABLE] RF_ID no válido\n");
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
			printf("[TABLE] No hay slots libres ni inactivos\n");
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
			printf("[TABLE] Error: No se pudo reservar memoria para data_buff\n");
			#endif /* VAMP_DEBUG */
			// Si falla la reserva, limpiar la entrada y retornar error
			vamp_table[table_index].status = VAMP_DEV_STATUS_FREE;
			return VAMP_MAX_DEVICES;
		}
		
		#ifdef VAMP_DEBUG
		printf("[TABLE] Buffer de datos reservado para dispositivo en índice %d\n", table_index);
		#endif /* VAMP_DEBUG */
	}
  
	return table_index;
}

/* Buscar el indice del dispositivo con "rf_id" */
uint8_t vamp_find_device(const uint8_t * rf_id) {
  for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
    if (memcmp(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN) == 0) {
      return i;
    }
  }
  return VAMP_MAX_DEVICES; // No encontrado
}

/* Remover dispositivo con "rf_id" de la tabla */
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

/* Obtener el índice del dispositivo inactivo más antiguo */
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



/** @todo REVISAR O LA PERTINENCIA DE ESTAS FUNCIONES O ELIMINAR */
/* --------------------- Manejo de perfiles -------------------- */

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



/* --------------------- Funciones auxiliares -------------------- */

/* Generar byte de ID compacto (verification + index) para un RF_ID */
uint8_t vamp_generate_id_byte(const uint8_t table_index) {

	/** Generar verificación del puerto a partir del numero que ya este
		en el slot del indice para evitar que sea el mismo */
	uint8_t check_bits = VAMP_GET_VERIFICATION(vamp_table[table_index].wsn_id);
	check_bits++; // Aumentar para evitar que sea el mismo

	// Generar ID byte: 3 bits de verificación + 5 bits de índice
	return (VAMP_MAKE_ID_BYTE(check_bits, table_index));
}

/* Verificar que el RF_ID sea válido */
bool is_valid_rf_id(const char * rf_id) {
  /* Verificar que el RF_ID no sea NULL y tenga la longitud correcta */
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

/* Convertir RF_ID a string hexadecimal */
void rf_id_to_hex(const uint8_t * rf_id, char * hex_str) {
  /* Convertir cada byte a un par de caracteres hexadecimales */
  for (int i = 0; i < VAMP_ADDR_LEN; i++) {
	snprintf(&hex_str[i * 2], 3, "%02X", rf_id[i]);
  }
  hex_str[VAMP_ADDR_LEN * 2] = '\0'; // Asegurar terminación
}

