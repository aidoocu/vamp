/** 
 * 
 * 
 */

#ifdef ARDUINO_ARCH_ESP8266

#include "vamp_json.h"

#include <ArduinoJson.h>
#include <cstring>
#include <cstdlib>

#include "../vamp_gw.h"

 /** @brief Parsear JSON object y llenar store */
bool vamp_kv_parse_json(vamp_key_value_store_t* store, JsonObject json_obj) {
    if (!store) return false;
    
    vamp_kv_clear(store); // Limpiar store antes de llenar
    
    for (JsonPair kv : json_obj) {
        const char* key = kv.key().c_str();
        const char* value = kv.value().as<const char*>();
        
        if (!vamp_kv_set(store, key, value)) {
            #ifdef VAMP_DEBUG
            Serial.print("Error añadiendo key-value: ");
            Serial.print(key);
            Serial.print(" = ");
            Serial.println(value);
            #endif
            // Continuar con el siguiente par aunque este falle
        }
    }
    
    return true;
}


/* Process the synchronization response from VREG.*/
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

	/* Verificar que el JSON es un objeto válido */
	if (!doc.is<JsonObject>()) {
		#ifdef VAMP_DEBUG
		Serial.println("JSON no es un objeto válido");
		#endif /* VAMP_DEBUG */
		return false;
	}

	/* Verificar que contiene los campos obligatorios */
	if (!doc.containsKey("timestamp") || !doc.containsKey("nodes")) {
		#ifdef VAMP_DEBUG
		Serial.println("JSON no contiene campos timestamp y/o nodes");
		#endif /* VAMP_DEBUG */
		return false;
	}

	/* Extraer el timestamp */
	const char* timestamp = doc["timestamp"];
	if (timestamp) {
		strncpy(last_table_update, timestamp, sizeof(last_table_update) - 1);
		last_table_update[sizeof(last_table_update) - 1] = '\0'; // Asegurar terminación
	}

	/* Procesar cada entrada en el array de nodos */
	JsonArray nodes = doc["nodes"];
	for (JsonObject node : nodes) {
		
		/* Verificar campos obligatorios y que no estén vacíos */
		if (!node.containsKey("action") || !node["action"] ||
			!node.containsKey("rf_id") || !node["rf_id"] || 
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
					if (profile["options"].is<JsonObject>()) {
						JsonObject options_obj = profile["options"];
						vamp_kv_parse_json(&vamp_table[table_index].profiles[profile_index].protocol_options, options_obj);
						
						#ifdef VAMP_DEBUG
						Serial.print("Protocol options parseadas: ");
						Serial.print(vamp_table[table_index].profiles[profile_index].protocol_options.count);
						Serial.println(" pares");
						#endif /* VAMP_DEBUG */
					} else {
						#ifdef VAMP_DEBUG
						Serial.println("Protocol options no es un objeto JSON válido");
						#endif /* VAMP_DEBUG */
					}
				}

				/* Extraer los protocols query */
				if (profile.containsKey("params")) {
					if (profile["params"].is<JsonObject>()) {
						JsonObject params_obj = profile["params"];
						vamp_kv_parse_json(&vamp_table[table_index].profiles[profile_index].query_params, params_obj);
						
						#ifdef VAMP_DEBUG
						Serial.print("Query params parseados: ");
						Serial.print(vamp_table[table_index].profiles[profile_index].query_params.count);
						Serial.println(" pares");
						#endif /* VAMP_DEBUG */
					} else {
						#ifdef VAMP_DEBUG
						Serial.println("Query params no es un objeto JSON válido");
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


#endif /* ARDUINO_ARCH_ESP8266 */