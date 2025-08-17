

/** @brief VAMP client
 * Cuando el nodo se despierta no tiene idea de si esta en una red o no,
 * ni cual es el gateway, por lo que su direccion de destino es
 * la direccion de broadcast. Lo que se hace es enviar un mensaje
 * de tipo VAMP_JOIN_REQ, que es un mensaje de solicitud de unión a la red
 * y el controlador de red responderá con un mensaje de tipo
 * VAMP_JOIN_ACK, que contiene la dirección MAC del gateway.
 * Asi la dirección de destino se actualiza a la dirección del gateway
 * y se puede enviar mensajes de tipo VAMP_DATA.
 */

#include "vamp_client.h"
#include "vamp_callbacks.h"

/* Dirección MAC del gateway por defecto, direccion de broadcast */
static uint8_t vamp_gw_addr[VAMP_ADDR_LEN] = VAMP_BROADCAST_ADDR;

/* Es como nos conoce el gateway 3 bits de verificación + 5 bits de dirección */
static uint8_t id_in_gateway = 0;

/* Contador de reintentos para detectar pérdida de conexión con gateway */
static uint8_t send_failure_count = 0;

/** @brief Buffer para almacenar datos de envío y recepción */
static uint8_t req_resp_wsn_buff[VAMP_MAX_PAYLOAD_SIZE];


void vamp_client_init(uint8_t * vamp_client_id) {

	/* El modo de radio RMODE_A esta por defecto */


	/* Inicializar la comunicación WSN */
	vamp_wsn_init(vamp_client_id);

	#ifdef VAMP_DEBUG
 	Serial.println("vclient id:");
	uint8_t * local_wsn_addr = vamp_get_local_wsn_addr();
	vamp_debug_msg(local_wsn_addr, VAMP_ADDR_LEN);
	#endif /* VAMP_DEBUG */
	
    /* Se intenta unir a la red VAMP */
    vamp_join_network();

}


/* Función para resetear la conexión con el gateway */
void vamp_reset_connection(void) {
	/* Resetear dirección del gateway a broadcast */
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		vamp_gw_addr[i] = 0xFF;
	}
	/* Resetear contador de fallos */
	send_failure_count = 0;
	id_in_gateway = 0;
}

/*  ----------------------------------------------------------------- */
bool vamp_is_joined(void) {
	// Verificar si la dirección del gateway es válida (no broadcast)
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		if (vamp_gw_addr[i] != 0xFF) {
			return true; // Si la dirección del gateway no es la de broadcast, está unido
		}
	}
	return false; // Si la dirección del gateway es la de broadcast, no está unido
}

/*  ----------------------------------------------------------------- */
/* Función para forzar un re-join (útil para testing o recuperación manual) */
bool vamp_force_rejoin(void) {
	vamp_reset_connection();
	return vamp_join_network();
}

/*  ----------------------------------------------------------------- */
bool vamp_join_network(void) {
	// Verificar si ya se ha unido previamente
	if (vamp_is_joined()) {
		return true; // Ya está unido, no es necesario volver a unirse
	}

	/* Resetear la conexión */
	vamp_reset_connection();

	#ifdef VAMP_DEBUG
	Serial.print("GW? ");
	vamp_debug_msg(vamp_gw_addr, VAMP_ADDR_LEN);
	#endif /* VAMP_DEBUG */

	uint8_t payload_len = 0;

	/*  Pseudoencabezado: T=1 (comando), Comando ID=0x01 (JOIN_REQ) = 0x81 */
	req_resp_wsn_buff[payload_len++] = VAMP_JOIN_REQ | VAMP_IS_CMD_MASK;

	/* Copiar dirección local */
	uint8_t * local_wsn_addr = vamp_get_local_wsn_addr();
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		req_resp_wsn_buff[payload_len++] = local_wsn_addr[i];
	}

	#ifdef VAMP_DEBUG
	Serial.println("Joining");
	#endif /* VAMP_DEBUG */

	/*  Enviar mensaje de unión al gateway */
	payload_len = vamp_wsn_comm(vamp_gw_addr, req_resp_wsn_buff, payload_len);
	/* El mensaje recibido debe ser un JOIN_ACK (0x82) + ID_IN_GW (1 byte) + dirección del gateway (5 bytes) */
	if ((!payload_len) || (req_resp_wsn_buff[0] != (VAMP_JOIN_ACK | VAMP_IS_CMD_MASK)) || (payload_len != (2 + VAMP_ADDR_LEN))) {
		Serial.println("JOIN_REQ failed");
		return false;
	}

	/* Extraer el ID en el gateway desde la respuesta */
	id_in_gateway = req_resp_wsn_buff[1];

	/*  Extraer la dirección MAC del gateway desde la respuesta */
	for (int i = 0; i < VAMP_ADDR_LEN; i++) {
		vamp_gw_addr[i] = req_resp_wsn_buff[i + 2]; // Copiar la dirección MAC del gateway

		/* Verificar que la dirección del gateway sea válida */
		if (!vamp_is_rf_id_valid(vamp_gw_addr)) {
			#ifdef VAMP_DEBUG
			Serial.println("gw addr invalid");
			#endif /* VAMP_DEBUG */

			/* 	Resetear conexión si la dirección es inválida para mantener los chequeos
				consistentes */
			vamp_reset_connection();
			return false;
		}
	}

	#ifdef VAMP_DEBUG
	Serial.print("joined! GW: ");
	vamp_debug_msg(vamp_gw_addr, VAMP_ADDR_LEN);
	#endif /* VAMP_DEBUG */

	/* Resetear contador de fallos ya que tenemos nueva conexión */
	send_failure_count = 0;

	return true; // Unión exitosa
}

/* Verificar si el cliente está activo en la red VAMP */
bool vamp_is_active(void) {
	/*  Verificar si ya se ha unido previamente, de lo contrario hay que volver a intentar
		volver a unirse al menos una vez */
	if (!vamp_is_joined()) {
		if (!vamp_join_network()) {
			/* Si no se pudo unir a la red, retornar falso */
			return false;
		}
	}
	return true;
}

/* Manejo de fallos en el envío */
bool vamp_fail_handle(void){

	/* Si el envío falla, incrementar contador de fallos */
	send_failure_count++;
	
	/* Si hay demasiados fallos consecutivos, resetear conexión */
	if (send_failure_count >= MAX_SEND_FAILURES) {
		vamp_reset_connection();
		
		/* Intentar re-join inmediatamente */
		if (vamp_join_network()) {
			send_failure_count = 0;
			return true; // Re-join exitoso
		}

		#ifdef VAMP_DEBUG
		Serial.println("vamp disconnected");
		#endif /* VAMP_DEBUG */

	}

	#ifdef VAMP_DEBUG
	Serial.println("comm failed");
	#endif /* VAMP_DEBUG */

	return false; // Fallo en el re-join o en el reenvío después de re-join
}

/*  ----------------------------------------------------------------- */

uint8_t vamp_client_tell(const uint8_t profile, const uint8_t * data, uint8_t len) {

	/* Verificar que los datos no sean nulos y esten dentro del rango permitido */
	if (profile >= VAMP_MAX_PROFILES || data == NULL || len == 0 || len >= VAMP_MAX_PAYLOAD_SIZE - 2) { // -2 min para el encabezado
		return 0;
	}
	
	/* Si el dispositivo no está activo, no se puede enviar el mensaje */
	if(!vamp_is_active()) {
		
		return 0;
	}

	/*  Crear mensaje de datos según el protocolo VAMP */
	//uint8_t payload_len = 0;

	/*  Pseudoencabezado: T=0 (datos), PP=(profile), LLLLL=(length) 
	 	En caso que len > VAMP_WSN_LENGTH_ESCAPE, se escribira 
		VAMP_WSN_LENGTH_ESCAPE en este primer len y se utilizará el
		tercer byte para indicar la longitud real del payload */
	uint8_t payload_len = 2;
	if (len < VAMP_WSN_LENGTH_ESCAPE) {
		req_resp_wsn_buff[0] = VAMP_WSN_MAKE_DATA_BYTE(profile, len);
	} else {
		req_resp_wsn_buff[0] = VAMP_WSN_MAKE_DATA_BYTE(profile, VAMP_WSN_LENGTH_ESCAPE);
		/* El tercer byte contendrá la longitud real */
		req_resp_wsn_buff[2] = len;
		payload_len = 3;
	}

	/* Copiar el identificador en el GW al segundo byte */
	req_resp_wsn_buff[1] = id_in_gateway;

	/*  Copiar los datos del payload */
	for (int i = 0; i < len; i++) {
		req_resp_wsn_buff[payload_len++] = data[i];
	}

	/*  Enviar el mensaje */    
	len = vamp_wsn_comm(vamp_gw_addr, req_resp_wsn_buff, payload_len);

	if (len == 0) {
		/* Si el envío falla, manejar el fallo */
		if(!vamp_fail_handle()) {
			/* Si el manejo del fallo también falla, retornar 0 */
			return 0;
		}
		/* Si el re-join fue exitoso, intentar enviar de nuevo */
		len = vamp_wsn_comm(vamp_gw_addr, req_resp_wsn_buff, payload_len);
		if(!len) {
			/* Si vuelve a fallar, incrementar contador de fallos y salir */
			send_failure_count ++;
			return 0;
		}

	}

	/* Envío exitoso, resetear contador de fallos */
	send_failure_count = 0;
	return len; // Envío exitoso

}

uint8_t vamp_client_tell(const uint8_t * data, uint8_t len){
	/* Hacer un tell con el profile por defecto */
	return vamp_client_tell(VAMP_DEFAULT_PROFILE, data, len);
}

bool vamp_client_ask(uint8_t profile) {
	/* Hacer un tell con el profile especificado y una cadena vacia */
	uint8_t data[1] = {'\0'};
	if (vamp_client_tell(profile, data, 1)) {
		return true;
	}
	return false;
}

bool vamp_client_ask(void) {
	/* Hacer un ask con el profile por defecto */
	return vamp_client_ask(VAMP_DEFAULT_PROFILE);
}

/* Simplemente enviar el comando de poll */
uint8_t vamp_client_poll(uint8_t * data, uint8_t len) {
	
	if(data == NULL || len == 0 || len >= VAMP_MAX_PAYLOAD_SIZE - 2) {
		return 0;
	}

	if (!vamp_is_active()) {
		/* Si el dispositivo no está activo en la red vamp, no se puede enviar el mensaje */
		return 0;
	}

	/*  Pseudoencabezado: T=1 (cmd), comando como tal */
	req_resp_wsn_buff[0] = (VAMP_POLL | VAMP_IS_CMD_MASK);

	/* Copiar el identificador en el GW al segundo byte */
	req_resp_wsn_buff[1] = id_in_gateway;

	/*  Enviar el mensaje */    
	uint8_t response_len = vamp_wsn_comm(vamp_gw_addr, req_resp_wsn_buff, 2);

	if(!response_len) {
		if(!vamp_fail_handle()) {
			/* Si el manejo del fallo también falla, retornar 0 */
			return 0;
		}
		/* Si el re-join fue exitoso, intentar enviar de nuevo */
		response_len = vamp_wsn_comm(vamp_gw_addr, req_resp_wsn_buff, 2);
		if(!response_len) {
			/* Si vuelve a fallar, incrementar contador de fallos y salir */
			send_failure_count ++;
			return 0;
		}
	}

	if(response_len > VAMP_MAX_PAYLOAD_SIZE) {
		/* Es una respuesta vacia, o un ACK, o un error en la respuesta */
		return 0;
	}

	/* Si ha llegado hasta aqui es que la respuesta es valida por lo que se pasa 
	   al buffer de recepción */
	memcpy(data, req_resp_wsn_buff, response_len);

	return response_len;

}