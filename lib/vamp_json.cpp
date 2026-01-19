/** 
 * 
 * 
 */

#ifdef ARDUINO_ARCH_ESP8266

#include "vamp_json.h"

#include <cstring>
#include <cstdlib>

//#include "vamp_kv.h"
#include "vamp_table.h"

#include "../vamp_gw.h"

/* Buffer JSON para parsear respuesta de VREG sync
 * Tamaño: Igual a VAMP_IFACE_BUFF_SIZE (el JSON siempre viene de iface_buff)
 * Con VAMP_IFACE_BUFF_SIZE=2048 y VAMP_MAX_DEVICES=8: ~250 bytes/device
 * Esto es suficiente ya que el JSON nunca puede ser mayor que iface_buff */
static StaticJsonDocument<VAMP_IFACE_BUFF_SIZE> doc;

 /** @brief Parsear JSON object y llenar store */
bool vamp_kv_parse_json(vamp_key_value_store_t* store, JsonObject json_obj) {
    if (!store) return false;
    
    vamp_kv_clear(store); // Limpiar store antes de llenar
    
    for (JsonPair kv : json_obj) {
        const char* key = kv.key().c_str();
        const char* value = kv.value().as<const char*>();
        
        if (!vamp_kv_set(store, key, value)) {
            #ifdef VAMP_DEBUG
            printf("[JSON] Error añadiendo key-value: %s = %s\n", key, value);
            #endif
            // Continuar con el siguiente par aunque este falle
        }
    }
    
    return true;
}

/* Procesar la respuesta de sincronización de VREG */
bool vamp_process_sync_json_response(const char* json_data) {

	if (json_data == NULL) {
		return false;
	}
	
	/* Parsear el JSON */
	DeserializationError error = deserializeJson(doc, json_data);
	if (error) {
		#ifdef VAMP_DEBUG
		printf("[JSON] Error parseando JSON: %d\n", (int)error.code());
		#endif /* VAMP_DEBUG */
		return false;
	}

	/* Verificar que el JSON es un objeto válido */
	if (!doc.is<JsonObject>()) {
		#ifdef VAMP_DEBUG
		printf("[JSON] JSON no es un objeto válido\n");
		#endif /* VAMP_DEBUG */
		return false;
	}

	/* Verificar que contiene los campos obligatorios */
	if (!doc.containsKey("timestamp") || !doc.containsKey("nodes")) {
		#ifdef VAMP_DEBUG
		printf("[JSON] JSON no contiene campos timestamp y/o nodes\n");
		#endif /* VAMP_DEBUG */
		return false;
	}

	/* Extraer el timestamp */
	const char* timestamp = doc["timestamp"];

	/** @todo !!!!! Aqui hay un problema con el manejo de timestamps y la actualizacion
	 * exitosa o no, esto no esta bien manejado asi y puede llevar a inconsistencias !!!!!*/
	if (timestamp) {
		vamp_set_last_sync_timestamp(timestamp);
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
			printf("[JSON] Entrada JSON sin mandatory fields o con valores vacíos\n");
			#endif /* VAMP_DEBUG */
			continue; // Saltar esta entrada
		}

		/* Extraer RF_ID */
		uint8_t rf_id[VAMP_ADDR_LEN];
		if (!hex_to_rf_id(node["rf_id"], rf_id)) {
			#ifdef VAMP_DEBUG
			printf("[JSON] RF_ID inválido en la respuesta VREG\n");
			#endif /* VAMP_DEBUG */
			continue;
		}

		#ifdef VAMP_DEBUG
		printf("[JSON] RF_ID recibido: %s\n", node["rf_id"].as<const char*>());
		#endif /* VAMP_DEBUG */

		/* Buscar si el nodo ya esta en la tabla */
		uint8_t table_index = vamp_find_device(rf_id);
		if (table_index < VAMP_MAX_DEVICES) {
			#ifdef VAMP_DEBUG
			printf("[JSON] Nodo registrado %s\n", node["rf_id"].as<const char*>());
			#endif /* VAMP_DEBUG */
		}
		if (table_index > VAMP_MAX_DEVICES) {
			#ifdef VAMP_DEBUG
			printf("[JSON] Error buscando al nodo en la tabla\n");
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
				printf("[JSON] Error agregando nodo a la tabla: %s - Sin slots disponibles o error interno\n", node["rf_id"].as<const char*>());
				#endif /* VAMP_DEBUG */
				continue; // Saltar este dispositivo y continuar con el siguiente
			}

			#ifdef VAMP_DEBUG
			printf("[JSON] Nodo agregado %s\n", node["rf_id"].as<const char*>());
			#endif /* VAMP_DEBUG */

			vamp_entry_t * entry = vamp_get_table_entry(table_index);
			if (!entry) {
				#ifdef VAMP_DEBUG
				printf("[JSON] Error al obtener la entrada de la tabla\n");
				#endif /* VAMP_DEBUG */
				continue;
			}

			/* Asignar el estado de cache */
			entry->status = VAMP_DEV_STATUS_CACHE;

			/* Extraer tipo del dispositivo */		
			if (!strcmp(node["type"], "fixed")) {
				entry->type = 0;
			} else if (!strcmp(node["type"], "dynamic")) {
				entry->type = 1;
			} else if (!strcmp(node["type"], "auto")) {
				entry->type = 2;
			} else {
				/* !!!!! valor por defecto ???? */
				#ifdef VAMP_DEBUG
				printf("[JSON] Tipo de dispositivo desconocido: %s\n", node["type"].as<const char*>());
				#endif /* VAMP_DEBUG */
				entry->type = 0; // Valor por defecto
			}

			/* Procesar perfiles, debe haber al menos uno */
			JsonArray profiles = node["profiles"];
			if (profiles.size() == 0 || profiles.size() >= VAMP_MAX_PROFILES) {
				#ifdef VAMP_DEBUG
				printf("[JSON] Tiene que haber entre 1 y %d perfiles\n", VAMP_MAX_PROFILES);
				#endif /* VAMP_DEBUG */
				continue; // Saltar este nodo si no hay perfiles
			}

			/* Inicializar contador de perfiles */
			uint8_t profile_index = 0;
			entry->profile_count = 0;

			/* Procesar cada perfil */
			for (JsonObject profile : profiles) {
				if (profile_index >= VAMP_MAX_PROFILES) {
					#ifdef VAMP_DEBUG
					printf("[JSON] Límite máximo de perfiles alcanzado\n");
					#endif /* VAMP_DEBUG */
					break;
				}

				/* Extraer method */
				if (profile.containsKey("method")) {

					/* Extraer el metodo (GET, POST...) */
					if (!strcmp(profile["method"], "GET")) {
						entry->profiles[profile_index].method = VAMP_HTTP_METHOD_GET;
					} else if (!strcmp(profile["method"], "POST")) {
						entry->profiles[profile_index].method = VAMP_HTTP_METHOD_POST;
					} else if (!strcmp(profile["method"], "PUT")) {
						entry->profiles[profile_index].method = VAMP_HTTP_METHOD_PUT;
					} else if (!strcmp(profile["method"], "DELETE")) {
						entry->profiles[profile_index].method = VAMP_HTTP_METHOD_DELETE;
					} else {
						#ifdef VAMP_DEBUG
						printf("[JSON] Método desconocido: %s\n", profile["method"].as<const char*>());
						#endif /* VAMP_DEBUG */
						/* !!! no me gustan estos valores por defecto !!!  */
						entry->profiles[profile_index].method = 0; // Valor por defecto
					}
				} else {
					/* !!! no me gustan estos valores por defecto !!!  */
					entry->profiles[profile_index].method = 0; // Valor por defecto
				}

				/* Extraer endpoint_resource */
				if (profile.containsKey("endpoint")) {
					const char * endpoint_str = profile["endpoint"];
					if (endpoint_str && strlen(endpoint_str) > 0 && strlen(endpoint_str) < VAMP_ENDPOINT_MAX_LEN) {
						/* Liberar memoria previa si existe */
						if (entry->profiles[profile_index].endpoint_resource) {
							free(entry->profiles[profile_index].endpoint_resource);
						}
						/* Asignar nueva memoria */
						entry->profiles[profile_index].endpoint_resource = strdup(endpoint_str);
						if (!entry->profiles[profile_index].endpoint_resource) {
							#ifdef VAMP_DEBUG
							printf("[JSON] Error asignando memoria para endpoint_resource\n");
							#endif /* VAMP_DEBUG */
							break;
						}
					} else {
						#ifdef VAMP_DEBUG
						printf("[JSON] Endpoint resource inválido o demasiado largo\n");
						#endif /* VAMP_DEBUG */
					}
				}

				/* Extraer protocol_options */
				if (profile.containsKey("options")) {
					if (profile["options"].is<JsonObject>()) {
						JsonObject options_obj = profile["options"];
						
						/* Pre-asignar antes de parsear */
						if (!vamp_kv_preallocate(&entry->profiles[profile_index].protocol_options)) {
							#ifdef VAMP_DEBUG
							printf("[JSON] Error pre-asignando protocol_options\n");
							#endif
							break;
						}
						
						vamp_kv_parse_json(&entry->profiles[profile_index].protocol_options, options_obj);

						#ifdef VAMP_DEBUG
						printf("[JSON] Protocol options parseadas: %d pares\n", entry->profiles[profile_index].protocol_options.count);
						#endif /* VAMP_DEBUG */
					} else {
						#ifdef VAMP_DEBUG
						printf("[JSON] Protocol options no es un objeto JSON válido\n");
						#endif /* VAMP_DEBUG */
					}
				}

				/* Extraer los protocols query */
				if (profile.containsKey("params")) {
					if (profile["params"].is<JsonObject>()) {
						JsonObject params_obj = profile["params"];
						
						/* Pre-asignar antes de parsear */
						if (!vamp_kv_preallocate(&entry->profiles[profile_index].query_params)) {
							#ifdef VAMP_DEBUG
							printf("[JSON] Error pre-asignando query_params\n");
							#endif
							break;
						}
						
						vamp_kv_parse_json(&entry->profiles[profile_index].query_params, params_obj);

						#ifdef VAMP_DEBUG
						printf("[JSON] Query params parseados: %d pares\n", entry->profiles[profile_index].query_params.count);
						#endif /* VAMP_DEBUG */
					} else {
						#ifdef VAMP_DEBUG
						printf("[JSON] Query params no es un objeto JSON válido\n");
						#endif /* VAMP_DEBUG */
					}
				}

				profile_index++;
				entry->profile_count = profile_index;
			}

			#ifdef VAMP_DEBUG
			printf("[JSON] Dispositivo ADD procesado con %d perfiles\n", profile_index);
			#endif /* VAMP_DEBUG */


		} else if (strcmp(node["action"], "REMOVE") == 0) {

			if (table_index == VAMP_MAX_DEVICES) {
				/* Remover dispositivo de la tabla */
				vamp_clear_entry(table_index);
				#ifdef VAMP_DEBUG
				printf("[JSON] Dispositivo REMOVE procesado exitosamente\n");
				#endif /* VAMP_DEBUG */
			} else {
				#ifdef VAMP_DEBUG
				printf("[JSON] Dispositivo REMOVE no encontrado en tabla\n");
				#endif /* VAMP_DEBUG */
			}

		} else {
			#ifdef VAMP_DEBUG
			printf("[JSON] Acción desconocida en JSON: %s\n", node["action"].as<const char*>());
			#endif /* VAMP_DEBUG */
		}
	}

	return true; // Procesamiento exitoso
}


#endif /* ARDUINO_ARCH_ESP8266 */