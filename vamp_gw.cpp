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


#ifdef ARDUINO_ARCH_ESP8266
#include "lib/vamp_json.h"  // Incluir header, no implementación
#include <cstring>
#include <cstdlib>
#endif

/* Profile del recurso VREG */
static vamp_profile_t vamp_vreg_profile;


/* ID del gateway */
static char vamp_gw_id[VAMP_GW_ID_MAX_LEN];


/* Buffer para la solicitud y respuesta de internet */
static char req_resp_internet_buff[VAMP_IFACE_BUFF_SIZE];


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
void vamp_gw_vreg_init(char * vreg_url, char * gw_id){

	/* Verificar que los parámetros son válidos */
	if (vreg_url == NULL || gw_id == NULL || 
	    strlen(vreg_url) >= VAMP_ENDPOINT_MAX_LEN || 
	    strlen(gw_id) >= VAMP_GW_ID_MAX_LEN) {

		#ifdef VAMP_DEBUG
		Serial.println("Parámetros inválidos para la inicialización de VAMP");
		#endif /* VAMP_DEBUG */
		return;
	}

	vamp_vreg_profile.method = VAMP_HTTP_METHOD_GET;
	if (vamp_vreg_profile.endpoint_resource) {
		free(vamp_vreg_profile.endpoint_resource);
	}
	vamp_vreg_profile.endpoint_resource = strdup(vreg_url);
	if (!vamp_vreg_profile.endpoint_resource) {
		#ifdef VAMP_DEBUG
		Serial.println("Error al asignar memoria para el recurso VREG");
		#endif /* VAMP_DEBUG */
		return;
	}

	// Inicializar y configurar protocol_options
	vamp_kv_init(&vamp_vreg_profile.protocol_options);
	vamp_kv_set(&vamp_vreg_profile.protocol_options, "Accept", "application/json");
	vamp_kv_set(&vamp_vreg_profile.protocol_options, "X-VAMP-Gateway-ID", gw_id);

	// Inicializar query_params (se configurará dinámicamente en vamp_table_update)
	vamp_kv_init(&vamp_vreg_profile.query_params);

}

/* preguntarle al VREG por el dispositivo "rf_id" !!!! no terminada */
uint8_t vamp_get_vreg_device(const uint8_t * rf_id) {

	/* Verificar que el RF_ID es válido */
	if (!vamp_is_rf_id_valid(rf_id)) {
		return VAMP_MAX_DEVICES;
	}

	/* Buffer para el ID del nodo, 10 caracteres (5 bytes en hex) + '/0' */
	char node_id[11];

	/* Convertir RF_ID a cadena hex */
	rf_id_to_hex(rf_id, node_id);

	// !!!hacer la solicitud aqui


	#ifdef VAMP_DEBUG
	Serial.print("Enviando ");
	Serial.println(req_resp_internet_buff);
	#endif /* VAMP_DEBUG */

	// Enviar request usando TELL y recibir respuesta
	if (vamp_iface_comm(&vamp_vreg_profile, req_resp_internet_buff, sizeof(req_resp_internet_buff))) {

		//procesar la respuesta aqui

	}
	#ifdef VAMP_DEBUG
	Serial.println("Error procesando respuesta VREG");
	#endif /* VAMP_DEBUG */
	return VAMP_MAX_DEVICES; // No se espera respuesta de datos
}

/* Procesar comando */
void vamp_gw_process_command(uint8_t *cmd, uint8_t len) {

}

/* --------------- WSN --------------- */

bool vamp_gw_wsn(void) {

	/* Buffer para datos WSN */
	static uint8_t wsn_buffer[VAMP_MAX_PAYLOAD_SIZE];

    /* Extraer el mensaje de la interface via callback */
	uint8_t data_recv = vamp_wsn_recv(wsn_buffer, VAMP_MAX_PAYLOAD_SIZE);

	if (data_recv == 0) {
		return false; // No hay datos disponibles
	}		

	/* Inicializar el puntero a la entrada 
	!!!!! esto no es del todo correcto asi !!!!! */
	vamp_entry_t * entry = NULL;

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

					entry = vamp_get_table_entry(table_index);
					/* Reportamos al nodo solicitante */
					vamp_wsn_send(entry->rf_id, wsn_buffer, 2 + VAMP_ADDR_LEN);

				} else {
					#ifdef VAMP_DEBUG
					Serial.print("Dispositivo encontrado en cache, index: ");
					Serial.println(entry->wsn_id);
					#endif /* VAMP_DEBUG */

					/* Formamos la respuesta para el nodo solicitante */
					/* El comando JOIN_ACK es 0x02, byte completo es 0x82 */
					wsn_buffer[0] = VAMP_JOIN_ACK | VAMP_IS_CMD_MASK;
					/* Enviar el identificador del nodo WSN en el gateway */
					wsn_buffer[1] = entry->wsn_id; // Asignar el ID del nodo WSN
					/* Asignar el ID del gateway */
					for (int i = 0; i < VAMP_ADDR_LEN; i++) {
						uint8_t * local_wsn_addr = vamp_get_local_wsn_addr();
						wsn_buffer[i + 2] = (uint8_t)local_wsn_addr[i]; // Asignar el ID del gateway
					}
					/* Reportamos al nodo solicitante */
					if (vamp_wsn_send(entry->rf_id, wsn_buffer, 2 + VAMP_ADDR_LEN)) {
						entry->status = VAMP_DEV_STATUS_ACTIVE; // Marcar como activo
						entry->last_activity = millis(); // Actualizar última actividad
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
			case VAMP_POLL:
				/* Manejar el comando POLL */
				#ifdef VAMP_DEBUG
				Serial.println("Comando POLL recibido");
				#endif /* VAMP_DEBUG */

				/* buscar la entrada */
				entry = vamp_get_table_entry(wsn_buffer[1]);
				if (!entry) {
					#ifdef VAMP_DEBUG
					Serial.println("Entrada no encontrada");
					#endif /* VAMP_DEBUG */
					return false; // Entrada no encontrada
				}

				/* Despues viene el ticket */
				if ((uint16_t)(wsn_buffer[2] | (wsn_buffer[3] << 8)) == entry->ticket) {

					vamp_client_tell((uint8_t *)entry->data_buff, strlen(entry->data_buff) - 1);

					/* Si el ticket coincide, se puede procesar la solicitud */
					#ifdef VAMP_DEBUG
					Serial.print("Ticket found");
					Serial.println((wsn_buffer[2] | (wsn_buffer[3] << 8)));
					#endif /* VAMP_DEBUG */
				} else {
					/* Si el ticket no coincide, se ignora la solicitud */
					#ifdef VAMP_DEBUG
					Serial.println("ticket deprecate");
					#endif /* VAMP_DEBUG */
				}

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

		entry = vamp_get_table_entry(wsn_buffer[1]);
		if (!entry) {
			#ifdef VAMP_DEBUG
			Serial.println("Entrada no encontrada");
			#endif /* VAMP_DEBUG */
			return false; // Entrada no encontrada
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

		/** Como la respuesta del servidor puede demorar y 
		probablemente el mote no resuelva nada con ella
		le respondemos y ack para que el mote sepa que 
		al menos su tarea fue recibida 
		@note que por cada ACK que se envie se incrementa el ticket
		y como en este caso hay un solo buffer, pues se recuerda un
		solo ticket por cada comunicación, si no se hace polling el 
		ticket se pierde.
		*/		
		entry->ticket++;
		uint8_t ack_buffer[3] = { (uint8_t)(VAMP_ACK | VAMP_IS_CMD_MASK), (uint8_t)(entry->ticket & 0xFF), (uint8_t)((entry->ticket >> 8) & 0xFF) };
		vamp_wsn_send(entry->rf_id, ack_buffer, 3);


		/* Copiar los datos recibidos al buffer de internet si es que hay datos */
		if(rec_len > 0) {
			memcpy(req_resp_internet_buff, &wsn_buffer[data_offset], rec_len);
		}
		req_resp_internet_buff[rec_len] = '\0'; // Asegurar terminación de cadena si es necesario

		/* Enviar con el perfil completo (método/endpoint/params) */
		if (profile->endpoint_resource && profile->endpoint_resource[0] != '\0') {
			if(vamp_iface_comm(profile, req_resp_internet_buff, rec_len)) {
				#ifdef VAMP_DEBUG
				Serial.println("Datos enviados a internet");
				#endif /* VAMP_DEBUG */
				
				/* !!!!!! hay que ver que pasa si se puede enviar y si hay respuesta */

				//hay que dejar fuera el '\0'
				memcpy(entry->data_buff, req_resp_internet_buff, rec_len); // Copiar datos al buffer del dispositivo

				return true;
			}
		} else {
			#ifdef VAMP_DEBUG
			Serial.println("Endpoint resource vacío, no se envía a internet");
			#endif /* VAMP_DEBUG */
		}


	}

	return true; // Procesamiento exitoso
}

