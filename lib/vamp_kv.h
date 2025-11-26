/** 
 * @file vamp_kv.h
 * @brief VAMP Key-Value Store
 * @version 1.0
 * @date 2025-07-15
 * @author Bernardo Yaser León Ávila
 *
 * Este archivo contiene las definiciones y declaraciones para el almacenamiento
 * de pares clave-valor en el contexto de VAMP.
 */

#include <Arduino.h>

/** @brief Configuración para pares key-value - Optimizado para ESP8266 */
#ifndef VAMP_MAX_KEY_VALUE_PAIRS
#define VAMP_MAX_KEY_VALUE_PAIRS 4  // Reducido de 8 a 4 pares máximo
#endif // VAMP_MAX_KEY_VALUE_PAIRS

#ifndef VAMP_KEY_MAX_LEN
#define VAMP_KEY_MAX_LEN 32  // Reducido de 32 a 16 caracteres
#endif // VAMP_KEY_MAX_LEN

#ifndef VAMP_VALUE_MAX_LEN
#define VAMP_VALUE_MAX_LEN 32  // Reducido de 64 a 32 caracteres
#endif // VAMP_VALUE_MAX_LEN

 /** Estructura para pares key-value */
typedef struct {
    char key[VAMP_KEY_MAX_LEN];
    char value[VAMP_VALUE_MAX_LEN];
} vamp_key_value_pair_t;

/** Estructura para almacenar múltiples pares key-value - Asignación dinámica */
typedef struct {
    vamp_key_value_pair_t* pairs;    // Puntero a array dinámico
    uint8_t count;                   // Número actual de pares
    uint8_t capacity;                // Capacidad máxima actual
} vamp_key_value_store_t;


/** @brief Inicializar un store de key-value */
void vamp_kv_init(vamp_key_value_store_t* store);

/** @brief Liberar memoria de un store de key-value */
void vamp_kv_free(vamp_key_value_store_t* store);

/** @brief Añadir o actualizar un par key-value */
bool vamp_kv_set(vamp_key_value_store_t* store, const char* key, const char* value);

/** @brief Obtener valor por clave */
const char* vamp_kv_get(const vamp_key_value_store_t* store, const char* key);

/** @brief Verificar si existe una clave */
bool vamp_kv_exists(const vamp_key_value_store_t* store, const char* key);

/** @brief Eliminar un par por clave */
bool vamp_kv_remove(vamp_key_value_store_t* store, const char* key);

/** @brief Limpiar todos los pares */
void vamp_kv_clear(vamp_key_value_store_t* store);

/** @brief Convertir store a string para HTTP headers */
size_t vamp_kv_to_http_headers(const vamp_key_value_store_t* store, char* buffer, size_t buffer_size);

/** @brief Convertir store a string para query parameters */
size_t vamp_kv_to_query_string(const vamp_key_value_store_t* store, char* buffer, size_t buffer_size);
