/** @brief VAMP Gateway
 * 
 * 
 * 
 * 
 */
#include "vamp.h"

// Tabla unificada VAMP (NAT + Device + Session)
static vamp_entry_t vamp_table[VAMP_MAX_DEVICES];

/* Fecha de la última actualización de la tabla en UTC */
static char last_table_update[] = {VAMP_TABLE_INIT_TSMP};

// Contadores globales
static uint8_t vamp_device_count = 0;

/* ======================== CALLBACKS ======================== */

/* Callback para comunicación http */

static vamp_http_callback_t vreg_comm_callback = NULL;
static vamp_radio_callback_t vreg_radio_callback = NULL;

/** @brief Register VREG communication callback
 * 
 * @param comm_cb Callback function for http communication
 * @param comm_radio Callback function for radio radio communication
 */
void vamp_set_callbacks(vamp_http_callback_t comm_cb, vamp_radio_callback_t comm_radio) {
  vreg_comm_callback = comm_cb;
  vreg_radio_callback = comm_radio;
  Serial.println("Callbacks comm registrado");
}



/* ======================== Inicialización ======================== */

/** @brief Initialize VAMP module
 * 
 * This function initializes the VAMP module, setting up the necessary callbacks
 * for HTTP and radio communication, and updating the VAMP table with the provided
 * VREG URL and gateway ID.
 * 
 * @param vamp_http_callback Callback function for HTTP communication with VREG
 * @param vamp_radio_callback Callback function for radio communication
 * @param vamp_vreg_url VREG server URL
 * @param vamp_gw_id Gateway ID string
 */
void vamp_init(vamp_http_callback_t vamp_http_callback, vamp_radio_callback_t vamp_radio_callback, const char * vamp_vreg_url, const char * vamp_gw_id) {

    /* Register the callbacks for HTTP and radio communication */
    vamp_set_callbacks(vamp_http_callback, vamp_radio_callback);

    /* Inicializando las tablas */
    vamp_table_update(vamp_vreg_url, vamp_gw_id);

    return;
}




// ======================== FUNCIONES VAMP TABLES ========================

// Declaraciones forward para funciones auxiliares
char* parse_csv_field(char** line_ptr);
bool hex_to_rf_id(const char* hex_str, uint8_t* rf_id);
bool vamp_process_sync_response(const char* csv_data);

// Inicializar todas las tablas VAMP con sincronización VREG
void vamp_table_update(const char* vreg_endpoint, const char* gateway_id) {

    // Ver cuando se actualizó la tabla por última vez
    if(!strcmp(last_table_update, VAMP_TABLE_INIT_TSMP)) {
        // La tabla no ha sido inicializada
        Serial.println("Tabla VAMP no inicializada, inicializando...");

        // Inicializar tabla unificada VAMP
        // Solo necesitamos verificar/setear el status - el resto se inicializa cuando se asigna
        for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
            vamp_table[i].status = VAMP_STATUS_FREE;
        }
        
        // Resetear contadores
        vamp_device_count = 0;
        
        Serial.println("Tabla VAMP inicializada localmente");
    }
    
    // Intentar sincronizar con VREG si hay callback y parámetros
    if (vreg_comm_callback != NULL && gateway_id != NULL && vreg_endpoint != NULL) {
        Serial.println("Sincronizando tabla VAMP con servidor VREG...");
        
        // Generar request de sincronización
        char sync_request[128];
        snprintf(sync_request, sizeof(sync_request), "VAMP_SYNC,%s,%s", gateway_id, last_table_update);
        
        // Buffer para respuesta - usar el mismo buffer para request y response
        char request_response_buffer[1024];
        strcpy(request_response_buffer, sync_request);
        
        // Enviar request usando POST y recibir respuesta
        if (vreg_comm_callback(vreg_endpoint, VAMP_HTTP_POST, request_response_buffer, sizeof(request_response_buffer))) {
            // El buffer ahora contiene la respuesta CSV
            if (vamp_process_sync_response(request_response_buffer)) {
                Serial.println("Sincronización VREG completada exitosamente");
            } else {
                Serial.println("Error procesando respuesta VREG");
            }
        } else {
            Serial.println("Error comunicándose con servidor VREG");
        }
    } else {
        // Si no hay callback pero la tabla necesita inicialización, usar timestamp actual
        if (!strcmp(last_table_update, VAMP_TABLE_INIT_TSMP)) {
            strcpy(last_table_update, "2025-07-18T00:00:00Z");
        }
        
        if (gateway_id != NULL || vreg_endpoint != NULL) {
            Serial.println("Callback VREG no registrado - sincronización omitida");
        }
    }
}// Obtener identificador de dispositivo por RF_ID
bool vamp_get_device_id(const uint8_t* rf_id, char* identifier) {
  for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
    if (vamp_table[i].status == VAMP_STATUS_ACTIVE && 
        memcmp(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN) == 0) {
      // Crear identificador: índice (formato: XX)
      snprintf(identifier, 8, "%02d", i);
      return true;
    }
  }
  return false; // No encontrado
}

// Obtener identificador de dispositivo por índice de tabla
bool vamp_get_device_id_by_index(int table_index, char* identifier) {
  if (table_index < 0 || table_index >= VAMP_MAX_DEVICES) {
    return false;
  }
  
  if (vamp_table[table_index].status == VAMP_STATUS_FREE) {
    return false;
  }
  
  // Crear identificador: índice (formato: XX)
  snprintf(identifier, 8, "%02d", table_index);
  return true;
}

// ======================== FUNCIONES ID COMPACTO ========================

// Generar byte de ID compacto (verification + index) para un RF_ID
uint8_t vamp_generate_id_byte(const uint8_t* rf_id) {
  // Buscar dispositivo existente
  for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
    if (vamp_table[i].status == VAMP_STATUS_ACTIVE && 
        memcmp(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN) == 0) {
      // Extraer verificación del puerto existente
      uint8_t verification = VAMP_GET_VERIFICATION((vamp_table[i].port - VAMP_PORT_BASE) >> 5);
      return VAMP_MAKE_ID_BYTE(verification, i);
    }
  }
  
  // Buscar slot libre
  for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
    if (vamp_table[i].status == VAMP_STATUS_FREE) {
      // Generar número de verificación aleatorio (1-7, evitar 0)
      uint8_t verification = (millis() % 7) + 1;
      
      // Asignar entrada
      memcpy(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN);
      vamp_table[i].port = VAMP_MAKE_PORT(verification, i);
      vamp_table[i].join_time = millis();
      vamp_table[i].last_activity = millis();
      vamp_table[i].status = VAMP_STATUS_ACTIVE;
      vamp_table[i].retry_count = 0;
      vamp_device_count++;
      
      return VAMP_MAKE_ID_BYTE(verification, i);
    }
  }
  
  return 0xFF; // Error: no hay slots libres
}

// Validar byte de ID compacto contra RF_ID
bool vamp_validate_id_byte(uint8_t id_byte, const uint8_t* rf_id) {
  uint8_t index = VAMP_GET_INDEX(id_byte);
  uint8_t verification = VAMP_GET_VERIFICATION(id_byte);
  
  if (index >= VAMP_MAX_DEVICES) {
    return false;
  }
  
  // Verificar que el slot esté activo
  if (vamp_table[index].status != VAMP_STATUS_ACTIVE) {
    return false;
  }
  
  // Verificar número de verificación comparando con puerto
  uint8_t port_verification = VAMP_GET_VERIFICATION((vamp_table[index].port - VAMP_PORT_BASE) >> 5);
  if (port_verification != verification) {
    return false;
  }
  
  // Verificar RF_ID
  if (memcmp(vamp_table[index].rf_id, rf_id, VAMP_ADDR_LEN) != 0) {
    return false;
  }
  
  return true;
}

// Obtener puerto NAT por byte de ID
uint16_t vamp_get_port_by_id_byte(uint8_t id_byte) {
  uint8_t index = VAMP_GET_INDEX(id_byte);
  uint8_t verification = VAMP_GET_VERIFICATION(id_byte);
  
  if (index >= VAMP_MAX_DEVICES || 
      vamp_table[index].status != VAMP_STATUS_ACTIVE) {
    return 0; // Puerto inválido
  }
  
  // Verificar que la verificación coincida con el puerto almacenado
  uint8_t port_verification = VAMP_GET_VERIFICATION((vamp_table[index].port - VAMP_PORT_BASE) >> 5);
  if (port_verification != verification) {
    return 0; // Puerto inválido
  }
  
  return vamp_table[index].port;
}

// Buscar dispositivo por byte de ID
vamp_entry_t* vamp_find_device_by_id_byte(uint8_t id_byte) {
  uint8_t index = VAMP_GET_INDEX(id_byte);
  uint8_t verification = VAMP_GET_VERIFICATION(id_byte);
  
  if (index >= VAMP_MAX_DEVICES) {
    return NULL;
  }
  
  if (vamp_table[index].status == VAMP_STATUS_ACTIVE) {
    // Verificar que la verificación coincida con el puerto almacenado
    uint8_t port_verification = VAMP_GET_VERIFICATION((vamp_table[index].port - VAMP_PORT_BASE) >> 5);
    if (port_verification == verification) {
      return &vamp_table[index];
    }
  }
  
  return NULL;
}

// Asignar índice de tabla a RF_ID
uint8_t vamp_assign_device_index(const uint8_t* rf_id) {
  // Usar la nueva función de generación de ID compacto
  uint8_t id_byte = vamp_generate_id_byte(rf_id);
  
  if (id_byte == 0xFF) {
    return 255; // No hay slots libres
  }
  
  return VAMP_GET_INDEX(id_byte);
}

// Agregar dispositivo a la tabla
bool vamp_add_device(const uint8_t* rf_id) {
  // Verificar si ya existe
  vamp_entry_t* existing = vamp_find_device(rf_id);
  if (existing != NULL) {
    // Actualizar dispositivo existente
    existing->last_activity = millis();
    existing->status = VAMP_STATUS_ACTIVE;
    return true;
  }
  
  // Generar ID compacto para nuevo dispositivo
  uint8_t id_byte = vamp_generate_id_byte(rf_id);
  return (id_byte != 0xFF);
}

// Remover dispositivo de la tabla
bool vamp_remove_device(const uint8_t* rf_id) {
  for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
    if (memcmp(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN) == 0 &&
        vamp_table[i].status != VAMP_STATUS_FREE) {
      vamp_clear_entry(i);
      return true;
    }
  }
  return false; // No encontrado
}

// Buscar dispositivo por RF_ID
vamp_entry_t* vamp_find_device(const uint8_t* rf_id) {
  for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
    if (memcmp(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN) == 0 && 
        vamp_table[i].status == VAMP_STATUS_ACTIVE) {
      return &vamp_table[i];
    }
  }
  return NULL; // No encontrado
}

// Limpiar una entrada específica de la tabla
void vamp_clear_entry(int index) {
  if (index < 0 || index >= VAMP_MAX_DEVICES) {
    return;
  }
  
  if (vamp_table[index].status != VAMP_STATUS_FREE) {
    // Solo marcar como libre - otros campos se sobrescriben cuando se reasigna
    vamp_table[index].status = VAMP_STATUS_FREE;
    vamp_device_count--;
  }
}

// Limpiar entradas expiradas
void vamp_cleanup_expired() {
  uint32_t current_time = millis();
  
  // Limpiar tabla unificada VAMP
  for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
    if (vamp_table[i].status == VAMP_STATUS_ACTIVE) {
      // Verificar timeout de dispositivo
      if (current_time - vamp_table[i].last_activity > VAMP_DEVICE_TIMEOUT) {
        vamp_clear_entry(i);
      }
    }
  }
}












// ======================== PARSING RESPUESTAS VREG ========================

// Parser simple para CSV (campo por campo)
char* parse_csv_field(char** line_ptr) {
  if (*line_ptr == NULL) return NULL;
  
  char* field_start = *line_ptr;
  char* comma = strchr(*line_ptr, ',');
  
  if (comma != NULL) {
    *comma = '\0';
    *line_ptr = comma + 1;
  } else {
    *line_ptr = NULL; // Último campo
  }
  
  return field_start;
}

// Convertir string hexadecimal a RF_ID
bool hex_to_rf_id(const char* hex_str, uint8_t* rf_id) {
  if (strlen(hex_str) != VAMP_ADDR_LEN * 2) {
    return false;
  }
  
  for (int i = 0; i < VAMP_ADDR_LEN; i++) {
    char hex_byte[3] = {hex_str[i*2], hex_str[i*2+1], '\0'};
    rf_id[i] = (uint8_t)strtol(hex_byte, NULL, 16);
  }
  
  return true;
}

// Procesar respuesta CSV de VREG
bool vamp_process_sync_response(const char* csv_data) {
  if (csv_data == NULL) {
    return false;
  }
  
  // Hacer una copia local para parsing
  size_t data_len = strlen(csv_data);
  char* local_copy = (char*)malloc(data_len + 1);
  if (local_copy == NULL) {
    return false;
  }
  strcpy(local_copy, csv_data);
  
  char* line = strtok(local_copy, "\n\r");
  char latest_timestamp[32] = {0};
  bool success = true;
  
  while (line != NULL && success) {
    char* line_ptr = line;
    
    // Parsear campos CSV: timestamp,action,rf_id,port,verification
    char* timestamp = parse_csv_field(&line_ptr);
    char* action = parse_csv_field(&line_ptr);
    char* rf_id_str = parse_csv_field(&line_ptr);
    char* port_str = parse_csv_field(&line_ptr);
    char* verification_str = parse_csv_field(&line_ptr);
    
    if (timestamp == NULL || action == NULL || rf_id_str == NULL) {
      success = false;
      break;
    }
    
    // Actualizar timestamp más reciente
    if (strcmp(timestamp, latest_timestamp) > 0) {
      strncpy(latest_timestamp, timestamp, sizeof(latest_timestamp) - 1);
    }
    
    // Procesar según acción
    uint8_t rf_id[VAMP_ADDR_LEN];
    if (!hex_to_rf_id(rf_id_str, rf_id)) {
      success = false;
      break;
    }
    
    if (strcmp(action, "ADD") == 0) {
      if (port_str && verification_str) {
        uint16_t port = (uint16_t)atoi(port_str);
        (void)atoi(verification_str); // Suprimir warning - verification ya está en el puerto
        
        // Buscar slot libre y asignar
        for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
          if (vamp_table[i].status == VAMP_STATUS_FREE) {
            memcpy(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN);
            vamp_table[i].port = port;
            vamp_table[i].join_time = millis();
            vamp_table[i].last_activity = millis();
            vamp_table[i].status = VAMP_STATUS_ACTIVE;
            vamp_table[i].retry_count = 0;
            vamp_device_count++;
            break;
          }
        }
      }
    } else if (strcmp(action, "DEL") == 0) {
      // Remover dispositivo
      for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
        if (memcmp(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN) == 0 &&
            vamp_table[i].status != VAMP_STATUS_FREE) {
          vamp_clear_entry(i);
          break;
        }
      }
    } else if (strcmp(action, "UPD") == 0) {
      if (port_str && verification_str) {
        uint16_t port = (uint16_t)atoi(port_str);
        
        // Actualizar dispositivo existente
        for (int i = 0; i < VAMP_MAX_DEVICES; i++) {
          if (memcmp(vamp_table[i].rf_id, rf_id, VAMP_ADDR_LEN) == 0 &&
              vamp_table[i].status == VAMP_STATUS_ACTIVE) {
            vamp_table[i].port = port;
            vamp_table[i].last_activity = millis();
            break;
          }
        }
      }
    }
    
    line = strtok(NULL, "\n\r");
  }
  
  // Actualizar timestamp de última sincronización
  if (success && strlen(latest_timestamp) > 0) {
    strncpy(last_table_update, latest_timestamp, sizeof(last_table_update) - 1);
  }
  
  free(local_copy);
  return success;
}