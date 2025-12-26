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
    
    /* Necesita expandir la capacidad? */
    if (store->count >= store->capacity) {
        uint8_t new_capacity = (store->capacity == 0) ? 1 : store->capacity * 2;
        if (new_capacity > VAMP_MAX_KEY_VALUE_PAIRS) {
            new_capacity = VAMP_MAX_KEY_VALUE_PAIRS;
        }
        
        if (store->count >= new_capacity) {
            return false; /* Límite máximo alcanzado */
        }
        
        /* Reallocar memoria */
        vamp_key_value_pair_t* new_pairs = (vamp_key_value_pair_t*)realloc(
            store->pairs, new_capacity * sizeof(vamp_key_value_pair_t));
        if (!new_pairs) {
            return false; /* Error de memoria */
        }
        
        store->pairs = new_pairs;
        store->capacity = new_capacity;
        
        /* Limpiar nueva memoria */
        memset(&store->pairs[store->count], 0, 
               (new_capacity - store->count) * sizeof(vamp_key_value_pair_t));
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

