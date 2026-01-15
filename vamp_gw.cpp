/** @brief VAMP Gateway
 * 
 * 
 * 
 * 
 */
#include "vamp.h"
#include "vamp_gw.h"
#include "vamp_client.h"
#include "vamp_callbacks.h"

//#include "lib/vamp_kv.h"
#include "lib/vamp_table.h"

#include "arch/rtc/rtc.h"


#ifdef ARDUINO_ARCH_ESP8266
#include "lib/vamp_json.h"  // Incluir header, no implementación
#include <cstring>
#include <cstdlib>
#endif

/* Si incluimos la memoria dentro del hardware se 
utilizara ra guardar todas las lineas */
#ifdef VAMP_SD
#include <SD.h>
#endif /* VAMP_SD */

/* Profile del recurso VREG */
static vamp_profile_t vamp_vreg_profile;

static const gw_config_t * gateway_conf;

/* Buffer para datos WSN */
static uint8_t wsn_buffer[VAMP_MAX_PAYLOAD_SIZE];

/* Buffer para la solicitud y respuesta de internet (compartido en todo el módulo VAMP) */
char iface_buff[VAMP_IFACE_BUFF_SIZE];

/* StaticJsonDocument para construir payload - REUTILIZABLE (evita fragmentación de stack) */
#ifdef ARDUINOJSON_AVAILABLE
static StaticJsonDocument<256> json_payload_doc;
#endif


/* Inicializar la tabla VAMP con el perfil de VREG */
void vamp_table_init(void) {
    /* Inicializar la tabla VAMP */
    vamp_table_update(&vamp_vreg_profile);
}

/* Sincronizar la tabla VAMP con el VREG y limpiar dispositivos expirados */
void vamp_table_sync(void) {
    /* Synchronize VAMP Gateway with VREG */
    vamp_table_update(&vamp_vreg_profile);

    /* Detect expired VAMP devices */
    vamp_detect_expired();
}

/* Inicializar el perfil de VREG */
bool vamp_gw_vreg_init(const gw_config_t * gw_config){

	/* Guardando de forma local la configuracion del gateway */
	gateway_conf = gw_config;

	size_t url_len;

	if(!gateway_conf || 
	   !gateway_conf->vamp.gw_id || 
	   !gateway_conf->vamp.vreg_resource ||
		strnlen(gateway_conf->vamp.gw_id, VAMP_GW_ID_MAX_LEN) == 0 ||
	   	strnlen(gateway_conf->vamp.vreg_resource, VAMP_ENDPOINT_MAX_LEN) == 0 ||
	   	strnlen(gateway_conf->vamp.gw_id, VAMP_GW_ID_MAX_LEN) >= VAMP_GW_ID_MAX_LEN ||
	   	strnlen(gateway_conf->vamp.vreg_resource, VAMP_ENDPOINT_MAX_LEN) >= VAMP_ENDPOINT_MAX_LEN) {
		#ifdef VAMP_DEBUG
		printf("[GW] Invalid gateway configuration\n");
		#endif /* VAMP_DEBUG */
		return false;
	}

	/* Optener el tamaño que tendra la cadena con el URL completo, incluyendo el '\0' */
	url_len = strlen(gateway_conf->vamp.vreg_resource) + strlen(gateway_conf->vamp.gw_id) + 1;

	/* Verificar que los parámetros son válidos */
	if (url_len == 0 || 
	    url_len >= VAMP_ENDPOINT_MAX_LEN || 
	    strlen(gateway_conf->vamp.gw_id) >= VAMP_GW_ID_MAX_LEN) {

		#ifdef VAMP_DEBUG
		printf("[GW] Invalid parameters VAMP init\n");
		#endif /* VAMP_DEBUG */
		return false;
	}

	/* ------------------ Configurar el perfil de VREG  ------------------ */

	/* GET como método */
	vamp_vreg_profile.method = VAMP_HTTP_METHOD_GET;

	/* Copiar el endpoint al perfil local del vreg */
	vamp_vreg_profile.endpoint_resource = (char*)malloc(url_len + 1);
	if (!vamp_vreg_profile.endpoint_resource) {
		#ifdef VAMP_DEBUG
		printf("[GW] Error allocating memory for resource VREG\n");
		#endif /* VAMP_DEBUG */
		return false;
	}
	sprintf(vamp_vreg_profile.endpoint_resource, "%s%s", gw_config->vamp.vreg_resource, gw_config->vamp.gw_id);

	#ifdef VAMP_DEBUG
	printf("[GW] RF ID: %s\n", gateway_conf->vamp.gw_id);
	printf("[GW] Resource: %s\n", vamp_vreg_profile.endpoint_resource);
	#endif /* VAMP_DEBUG */

	/* ------------------ Configurar opciones del protocolo ------------------ */

	/* Inicializar los parámetros y las opciones del protocolo */
	vamp_kv_init(&vamp_vreg_profile.protocol_options);
	vamp_kv_init(&vamp_vreg_profile.query_params);

	/* Pre-asignar capacidad máxima ANTES de cualquier conexión TLS */
	if (!vamp_kv_preallocate(&vamp_vreg_profile.protocol_options)) {
		#ifdef VAMP_DEBUG
		printf("[GW] Failed to preallocate protocol_options\n");
		#endif
		return false;
	}
	
	if (!vamp_kv_preallocate(&vamp_vreg_profile.query_params)) {
		#ifdef VAMP_DEBUG
		printf("[GW] Failed to preallocate query_params\n");
		#endif
		return false;
	}

	#ifdef VAMP_DEBUG
	printf("[GW] vamp_kv pre-allocated successfully\n");
	#endif

	/* Agregar el ID del gateway en las opciones del protocolo */
	//vamp_kv_set(&vamp_vreg_profile.protocol_options, "X-VAMP-Gateway-ID", gw_id);

	return true;

}

/* preguntarle al VREG por el dispositivo "rf_id" !!!! por revisar */
uint8_t vamp_get_vreg_device(const uint8_t * rf_id) {

	/* Verificar que el RF_ID es válido */
	if (!vamp_is_rf_id_valid(rf_id)) {
		return VAMP_MAX_DEVICES;
	}

	/* Buffer para el ID del nodo, 10 caracteres (5 bytes en hex) + '/0' */
	char char_rf_id[VAMP_GW_ID_MAX_LEN] = {0};

	/* Convertir RF_ID a cadena hex */
	rf_id_to_hex(rf_id, char_rf_id);

	/* Configurar query_params con device */
	//vamp_clear_profile.query_params.pairs[0].key = NULL;
	//vamp_clear_profile.query_params.pairs[0].value = NULL;
	vamp_kv_clear(&vamp_vreg_profile.query_params);
	vamp_kv_set(&vamp_vreg_profile.query_params, "device", char_rf_id);

	// Enviar request usando TELL y recibir respuesta
	if (vamp_iface_comm(&vamp_vreg_profile, iface_buff, VAMP_IFACE_BUFF_SIZE)) {

		/* Extraer los datos JSON de la respuesta */
		#ifdef ARDUINOJSON_AVAILABLE
		if (vamp_process_sync_json_response(iface_buff)) {
			/* Buscar el dispositivo en la tabla */
			return vamp_find_device(rf_id);
		}
		#endif /* ARDUINOJSON_AVAILABLE */
	}

	#ifdef VAMP_DEBUG
	printf("[GW] Error procesando respuesta VREG\n");
	#endif /* VAMP_DEBUG */
	return VAMP_MAX_DEVICES; // No se espera respuesta de datos

}

/* Procesar comando "cmd" */
void vamp_gw_process_command(uint8_t * cmd, uint8_t len) {

	/* Aislar el comando */
	cmd[0] = cmd[0] & VAMP_WSN_CMD_MASK;
	uint8_t node_index = 0;

	/* Ahora buscamos uno por uno cual de los comandos puede ser */

	/* Comando JOIN_REQ */
	if (cmd[0] == VAMP_JOIN_REQ) {

		#ifdef VAMP_DEBUG
		printf("[GW] JOIN_REQ cmd\n");
		#endif /* VAMP_DEBUG */

		/* Verificamos que tenga el largo correcto */
		if (len != VAMP_JOIN_REQ_LEN) {
			#ifdef VAMP_DEBUG
			printf("[GW] JOIN_REQ inválido, largo incorrecto\n");
			#endif /* VAMP_DEBUG */
			return; // Comando inválido
		}

		/* Asumimos que el RF_ID está después del comando */
		node_index = vamp_find_device(&cmd[1]);

		/* Si indice es mayor o igual que el maximo de dispositivos es que NO se ha 
		encontrado en el cache */
		if (node_index >= VAMP_MAX_DEVICES) {

			/* Dispositivo no encontrado en caché, hay que solicitarlo al VREG */
			#ifdef VAMP_DEBUG
			printf("[GW] no in cache, asking VREG\n");
			#endif /* VAMP_DEBUG */

					/* Si no esta en el cache hay que preguntarle al VREG */
			node_index = vamp_get_vreg_device(&cmd[1]);

			if (node_index == VAMP_MAX_DEVICES) {
				#ifdef VAMP_DEBUG
				printf("[GW] VREG: no device found\n");
				#endif /* VAMP_DEBUG */
				return; // Error al obtener el dispositivo
			}

			#ifdef VAMP_DEBUG
			printf("[GW] Dev found in VREG: ");
			#endif /* VAMP_DEBUG */


		} 
		#ifdef VAMP_DEBUG
		else {
			printf("Dev in cache: ");
		}
		#endif /* VAMP_DEBUG */
		
		vamp_entry_t * entry = vamp_get_table_entry(node_index);

		#ifdef VAMP_DEBUG
		printf("%d\n", entry->wsn_id);
		#endif /* VAMP_DEBUG */

		entry->status = VAMP_DEV_STATUS_REQUEST; // Marcar como en solicitud
		entry->last_activity = millis(); // Actualizar última actividad

		/* Formamos la respuesta para el nodo solicitante */

		/* El comando JOIN_OK es 0x02, byte completo es 0x82 */
		cmd[0] = VAMP_JOIN_OK | VAMP_IS_CMD_MASK;
		/* Enviar el identificador del nodo WSN en el gateway */
		cmd[1] = entry->wsn_id; // Asignar el ID del nodo WSN
		/* Asignar el ID del gateway */
		for (int i = 0; i < VAMP_ADDR_LEN; i++) {
			uint8_t * local_wsn_addr = vamp_get_local_wsn_addr();
			cmd[i + 2] = (uint8_t)local_wsn_addr[i]; // Asignar el ID del gateway
		}
		/* Reportamos al nodo solicitante */
		vamp_wsn_send(entry->rf_id, cmd, 2 + VAMP_ADDR_LEN);


		return;
	}

	/* Comando JOIN_OK */
	if (cmd[0] ==  VAMP_JOIN_OK) {

		#ifdef VAMP_DEBUG
		printf("[GW] JOIN_OK\n");
		#endif /* VAMP_DEBUG */

		vamp_entry_t * entry = vamp_get_table_entry(VAMP_GET_INDEX(cmd[1]));

		if (!entry) {
			#ifdef VAMP_DEBUG
			printf("[GW] JOIN_OK: entrada no encontrada\n");
			#endif /* VAMP_DEBUG */
			return; // Entrada no encontrada
		}

		if (entry->wsn_id != cmd[1]) {
			#ifdef VAMP_DEBUG
			printf("[GW] JOIN_OK: no coincide ID\n");
			#endif /* VAMP_DEBUG */
			return; // ID no válido
		}

		if (entry->status != VAMP_DEV_STATUS_REQUEST) {
			#ifdef VAMP_DEBUG
			printf("[GW] JOIN_OK: entrada no solicitada\n");
			#endif /* VAMP_DEBUG */
			return; // Estado no válido
		}

		/* Marcar como activo */
		entry->status = VAMP_DEV_STATUS_ACTIVE; 

		return;
	}

	/* Comando PING */
	if (cmd[0] ==  VAMP_PING) {

		#ifdef VAMP_DEBUG
		printf("[GW] PING\n");
		#endif /* VAMP_DEBUG */

		/* ... */

		return;

	}

	/* Comando PONG */
	if (cmd[0] ==  VAMP_PONG) {

		#ifdef VAMP_DEBUG
		printf("[GW] PONG\n");
		#endif /* VAMP_DEBUG */

		/* ... */

		return;

	}

	/* Comando POLL (no revisado) */
	if (cmd[0] ==  VAMP_POLL) {
		
		/* Manejar el comando POLL */
		#ifdef VAMP_DEBUG
		printf("[GW] Comando POLL recibido\n");
		#endif /* VAMP_DEBUG */

		/* buscar la entrada */
		vamp_entry_t * entry = vamp_get_table_entry(VAMP_GET_INDEX(cmd[1]));
		if (!entry) {
			#ifdef VAMP_DEBUG
			printf("[GW] Entrada no encontrada\n");
			#endif /* VAMP_DEBUG */
			return; // Entrada no encontrada
		}

		/* Despues viene el ticket */
		uint16_t ticket = cmd[3] | (cmd[2] << 8);

		#ifdef VAMP_DEBUG
		printf("[GW] Ticket recv: %d\n", ticket);
		#endif /* VAMP_DEBUG */

		if (ticket == entry->ticket) {
			/* Si el ticket coincide, se puede procesar la solicitud */
			#ifdef VAMP_DEBUG
			printf("[GW] and found it...\n");
			#endif /* VAMP_DEBUG */

			uint8_t response[VAMP_MAX_PAYLOAD_SIZE];
			response[0] = strlen(entry->data_buff);
			response[1] = 0xFF;
			memcpy(&response[2], entry->data_buff, response[0]);

			vamp_wsn_send(entry->rf_id, response, 2 + response[0]);

		} else {
			/* Si el ticket no coincide, se ignora la solicitud */
			#ifdef VAMP_DEBUG
			printf("[GW] ticket deprecate\n");
			#endif /* VAMP_DEBUG */
		}

	return;

	}

	/* Comando no reconocido */

	#ifdef VAMP_DEBUG
	printf("[WSN] Unknown cmd: %02X\n", cmd[0]);
	#endif /* VAMP_DEBUG */

	return; 

}

bool vamp_gw_process_data(uint8_t * data, uint8_t len) {

	/* El primer byte contiene el protocolo: [C=0][PP][LLLLL] */
	uint8_t profile_index = VAMP_WSN_GET_PROFILE(data[0]);
	uint8_t rec_len = VAMP_WSN_GET_LENGTH(data[0]);

	/* Manejar escape de longitud (si length == 31, el siguiente byte tiene la longitud real) */
	uint8_t data_offset = 2; // Por defecto: [protocol][wsn_id][data...]
	if (rec_len == VAMP_WSN_LENGTH_ESCAPE) {
		rec_len = data[2];
			data_offset = 3; // [protocol][wsn_id][length][data...]
		}

	/* Verificar que la longitud es válida */
	if ((rec_len > (VAMP_MAX_PAYLOAD_SIZE - data_offset)) || ((rec_len + data_offset) != len)) {
			#ifdef VAMP_DEBUG
			printf("[WSN] invalid data length: rec_len=%d, data_offset=%d, len=%d\n", rec_len, data_offset, len);
			#endif /* VAMP_DEBUG */
			return false; // Longitud inválida
		}

		/* Verificar que el perfil es válido */
		if (profile_index >= VAMP_MAX_PROFILES) {
			#ifdef VAMP_DEBUG
			printf("[WSN] invalid profile index: %d\n", profile_index);
			#endif /* VAMP_DEBUG */
			return false; // Perfil inválido
		}

		const uint8_t node_index = VAMP_GET_INDEX(data[1]);
		vamp_entry_t * entry = vamp_get_table_entry(node_index);

		if (!entry) {
			#ifdef VAMP_DEBUG
			printf("[WSN] entry not found for index: %d\n", node_index);
			#endif /* VAMP_DEBUG */
			return false; // Entrada no encontrada
		}

		if (entry->status != VAMP_DEV_STATUS_ACTIVE) {
			#ifdef VAMP_DEBUG
			printf("[WSN] entry not active for device: %02X\n", entry->wsn_id);
			#endif /* VAMP_DEBUG */
			return false; // Entrada no activa
		}

		/* Verificar que el dispositivo tiene el perfil solicitado */
		const vamp_profile_t * profile = &entry->profiles[profile_index];
		if (!profile) {
			#ifdef VAMP_DEBUG
			printf("[WSN] profile %d not configured for device: %02X\n", profile_index, entry->wsn_id);
			#endif /* VAMP_DEBUG */
			return false; // Perfil no configurado
		}

		/* Actualizar la última actividad del dispositivo */
		entry->last_activity = millis();

		#ifdef VAMP_DEBUG
		printf("[WSN] received from device %02X, profile %d, resource: %s\n", 
				(entry->wsn_id & 0x7F), 
				profile_index, 
					profile->endpoint_resource ? profile->endpoint_resource : "N/A");
		printf("[WSN] data: %s\n", &data[data_offset]);
		//vamp_debug_msg(&data[data_offset], rec_len);
		#endif /* VAMP_DEBUG */

		/** Como la respuesta del servidor puede demorar y 
		probablemente el mote no resuelva nada con ella
		le respondemos con un TICKET para que el mote sepa que
		al menos su tarea fue recibida
		@note que por cada TICKET que se envie se incrementa el ticket
		y como en este caso hay un solo buffer, pues se recuerda un
		solo ticket por cada comunicación, si no se hace polling el 
		ticket se pierde.
		*/		
		entry->ticket++;
		vamp_wsn_send_ticket(entry->rf_id, entry->ticket);


		/** ---------------------- Enviar al endpoint ---------------------- * 
		* ToDo!!!
		* Esta parte queda como muy especifica de la aplicacion farm, pues se construye un json
		* con datetime, gw_id y data que es lo que este endpoint en particular espera
		* por lo tanto en una aplicacion generica esto habria que modificarlo
		*/
		/* ToDo HAY QUE RESISAR ESTO!!! */
		#ifdef ARDUINOJSON_AVAILABLE
	/* Reutilizar documento JSON estático (NO crear en stack cada vez) */
	json_payload_doc.clear();
	
	/* Obtener fecha/hora actual del RTC */
	char datetime_buf[DATE_TIME_BUFF];
	datetime_buf[0] = '\0';
	rtc_get_utc_time(datetime_buf);
	json_payload_doc["datetime"] = datetime_buf;

	/* Agregar gateway_id */
	json_payload_doc["gw"] = gateway_conf->vamp.gw_id ? gateway_conf->vamp.gw_id : "";
			
	/* Agregar datos */
	char to_send_data[VAMP_MAX_PAYLOAD_SIZE];
	to_send_data[0] = '\0';
	if (rec_len > 0 && rec_len < VAMP_MAX_PAYLOAD_SIZE) {
		memcpy(to_send_data, &data[data_offset], rec_len);
		to_send_data[rec_len] = '\0';
		json_payload_doc["data"] = to_send_data;
	} else {
		json_payload_doc["data"] = "";
	}

	/* Serializar JSON al buffer */
	size_t json_len = serializeJson(json_payload_doc, iface_buff, VAMP_IFACE_BUFF_SIZE - 1);
	iface_buff[json_len] = '\0';
	rec_len = json_len;

	/* HAY QUE RESISAR ESTO!!! */
	#endif /* ARDUINOJSON_AVAILABLE */

	/** ---------------------- /Enviar al endpoint ---------------------- */


	/* Enviar con el perfil completo (método/endpoint/params) */
		if (!profile->endpoint_resource || profile->endpoint_resource[0] == '\0') {
			#ifdef VAMP_DEBUG
			printf("[WSN] empty endpoint resource, not sending to internet\n");
			#endif /* VAMP_DEBUG */
			return false;
		}

		size_t rec_iface_len = vamp_iface_comm(profile, iface_buff, json_len);

		if(rec_iface_len > 0) {
			#ifdef VAMP_DEBUG
		printf("[GW] respuesta desde el endpoint\n");
		#endif /* VAMP_DEBUG */

			/* Verificar que el buffer fue asignado en vamp_add_device() */
			if (!entry->data_buff) {
				#ifdef VAMP_DEBUG
				printf("[GW] Error: data_buff no inicializado\n");
				#endif /* VAMP_DEBUG */
				return false;
			}

			/* Limitar al tamaño del buffer pre-asignado */
			if (rec_iface_len > VAMP_MAX_PAYLOAD_SIZE) {
				rec_iface_len = VAMP_MAX_PAYLOAD_SIZE;
			}

			/* Copiar datos al buffer pre-asignado */
			memcpy(entry->data_buff, iface_buff, rec_iface_len);
			entry->data_buff[rec_iface_len] = '\0'; // Asegurar terminación

			#ifdef VAMP_DEBUG
			printf("Datos recibidos del endpoint: %s\n", entry->data_buff);
			#endif /* VAMP_DEBUG */
			return true;

		}

	/* Procesamiento exitoso */
	return true;
}

/* --------------- WSN --------------- */

int8_t vamp_gw_wsn(void) {

    /* Extraer el mensaje de la interface via callback */
	int8_t recv_len = vamp_wsn_recv(wsn_buffer, VAMP_MAX_PAYLOAD_SIZE);

	if (recv_len <= 0) {
		return recv_len; // No hay datos disponibles
	}

	/* --------------------- Si es un comando --------------------- */
	if (wsn_buffer[0] & VAMP_IS_CMD_MASK) {

		#ifdef VAMP_DEBUG
		printf("[GW] Comando recibido del WSN\n");
		#endif /* VAMP_DEBUG */

		/* !!!!!!!!!!!!!!!! Si falla algun procesamiento de comando hay que manejarlo aqui !!!!!!! */
		/* Procesar el comando */
		vamp_gw_process_command(wsn_buffer, recv_len);
		return 2;
	}


	/* ----------------------- Si es un dato ----------------------- */

	if (vamp_gw_process_data(wsn_buffer, recv_len)) {
		return 1;
	}

	/* Procesamiento de datos fallido */
	return -3;
}

