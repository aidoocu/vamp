/** 
 * 
 * 
 */


#include "vamp_kv.h"
#include "../vamp_config.h"

/** @brief Inicializar un store de key-value */
void vamp_kv_init(vamp_key_value_store_t* store) {
    if (!store) return;
    store->pairs = NULL;
    store->count = 0;
    store->capacity = 0;
}

/** @brief Pre-asignar capacidad máxima permanente (sin fragmentación) */
bool vamp_kv_preallocate(vamp_key_value_store_t* store) {
    if (!store) return false;
    
    /* Si ya está asignado, no hacer nada */
    if (store->pairs != NULL && store->capacity > 0) {
        return true;
    }
    
    /* Alocar capacidad máxima de una vez */
    store->pairs = (vamp_key_value_pair_t*)malloc(
        VAMP_MAX_KEY_VALUE_PAIRS * sizeof(vamp_key_value_pair_t));
    
    if (!store->pairs) {
        #ifdef VAMP_DEBUG
        printf("[VAMP_KV] Failed to preallocate memory\n");
        #endif
        return false;
    }
    
    store->capacity = VAMP_MAX_KEY_VALUE_PAIRS;
    store->count = 0;
    
    /* Limpiar memoria */
    memset(store->pairs, 0, VAMP_MAX_KEY_VALUE_PAIRS * sizeof(vamp_key_value_pair_t));
    
    #ifdef VAMP_DEBUG
    printf("[VAMP_KV] Preallocated %d pairs (%d bytes)\n", 
           VAMP_MAX_KEY_VALUE_PAIRS, 
           VAMP_MAX_KEY_VALUE_PAIRS * sizeof(vamp_key_value_pair_t));
    #endif
    
    return true;
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

    #ifdef VAMP_DEBUG
    printf("[VAMP] Init/update key=value (%s=%s): ", key, value);
    #endif /* VAMP_DEBUG */

    /* Buscar si la key ya existe */
    for (uint8_t i = 0; i < store->count; i++) {
        if (strcmp(store->pairs[i].key, key) == 0) {
            /* Actualizar valor existente */
            strncpy(store->pairs[i].value, value, VAMP_VALUE_MAX_LEN - 1);
            store->pairs[i].value[VAMP_VALUE_MAX_LEN - 1] = '\0';
            return true;
        }
    }
    
    /* Verificar capacidad (si está pre-asignado, solo verificar límite) */
    if (store->count >= store->capacity) {
        #ifdef VAMP_DEBUG
        printf("[VAMP_KV] Capacity limit reached (%d/%d)\n", store->count, store->capacity);
        #endif
        return false; /* Límite máximo alcanzado */
    }
    
    /* Añadir nueva entrada */
    strncpy(store->pairs[store->count].key, key, VAMP_KEY_MAX_LEN - 1);
    store->pairs[store->count].key[VAMP_KEY_MAX_LEN - 1] = '\0';
    strncpy(store->pairs[store->count].value, value, VAMP_VALUE_MAX_LEN - 1);
    store->pairs[store->count].value[VAMP_VALUE_MAX_LEN - 1] = '\0';
    store->count++;

    #ifdef VAMP_DEBUG
	printf("(ADD) total: %u\n", store->count);
    #endif /* VAMP_DEBUG */

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

/** @brief Limpiar todos los pares (sin liberar memoria pre-asignada) */
void vamp_kv_clear(vamp_key_value_store_t* store) {
    if (!store) return;
    
    /* Solo resetear contador y limpiar contenido, NO liberar memoria */
    store->count = 0;
    
    /* Limpiar datos para evitar valores obsoletos */
    if (store->pairs && store->capacity > 0) {
        memset(store->pairs, 0, store->capacity * sizeof(vamp_key_value_pair_t));
    }
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

