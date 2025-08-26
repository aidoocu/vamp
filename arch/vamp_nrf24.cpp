/** @file nrf24.cpp
 *  @brief Implementación de la comunicación NRF24L01
 */

#include "../vamp.h"
#include "../vamp_callbacks.h"
#include "vamp_nrf24.h"



#ifdef RF24_AVAILABLE
#include <SPI.h>
#include <RF24.h>


/* CE, CSN pins */
RF24 wsn_radio;

/* Buffer para datos WSN */
static uint8_t nrf_buff [VAMP_MAX_PAYLOAD_SIZE]; 


/* Inicializar el nRF24 */
bool nrf_init(uint8_t ce_pin, uint8_t csn_pin, uint8_t * addr) {

	if (wsn_radio.begin(ce_pin, csn_pin)) {
		// Configuración mínima necesaria para funcionar
		wsn_radio.enableDynamicPayloads(); 	// NECESARIO para getDynamicPayloadSize()
		wsn_radio.disableAckPayload();		// Sin payload en ACKs
		wsn_radio.setAutoAck(false);		// DESHABILITAR ACKs completamente

		/* ✅ Pipe 0 direccion local */
		wsn_radio.openReadingPipe(0, addr);
		/* ✅ Pipe 1 acepta dirección completa diferente (broadcast) */
		uint8_t broadcast_addr[5] = VAMP_BROADCAST_ADDR;
		wsn_radio.openReadingPipe(1, broadcast_addr);
		
		wsn_radio.setAutoAck(1, false);
		wsn_radio.flush_rx();

		#ifdef VAMP_DEBUG
		Serial.println("NRF24 ready");
		#endif /* VAMP_DEBUG */

		/* Si es un gateway siempre debera estar escuchando */
		if (vamp_get_settings() & VAMP_RMODE_B) {
			// Modo siempre escucha
			wsn_radio.startListening(); // Modo siempre escucha
			#ifdef VAMP_DEBUG
			Serial.println("and listening");
			#endif /* VAMP_DEBUG */
			return true; // Éxito - salir de la función
		} 
			
		wsn_radio.stopListening(); // Modo bajo consumo
		return true; // Éxito - salir de la función
	}
	return false; // Fallo - salir de la función
}

void nrf_set_address(uint8_t * rf_id) {
	// Establecer dirección local del pipe de lectura
	wsn_radio.openReadingPipe(0, rf_id);
}

/** @brief Lee datos del nRF24
 * 
 * @return 	Número de bytes leídos si hay datos disponibles, 
 * 			0 en caso contrario
 */
uint8_t nrf_read(void) {

	/* Recibiendo datos desde el nRF24 */
	if (!wsn_radio.available()) {
		return 0;
	}
	
	/* Leer datos del nRF24 */
	uint8_t bytes_read = wsn_radio.getDynamicPayloadSize();
	if (!bytes_read)
		return 0;
	if (bytes_read > VAMP_MAX_PAYLOAD_SIZE) 
		bytes_read = VAMP_MAX_PAYLOAD_SIZE;
	wsn_radio.read(nrf_buff, bytes_read);

	return bytes_read; // Éxito al recibir datos
}

/** 
 * Escuchar en una ventana por si hay alguna solicitud TICKET
 * @return 	Número de bytes leídos si hay datos disponibles, 
 * 			0 en caso contrario
 * @note	Esta función abre una ventana de escucha y espera por datos.
 */
uint8_t nrf_listen_window(void) {
	
	wsn_radio.flush_rx();
	wsn_radio.startListening();

	uint8_t bytes_read = 0;

	uint32_t start_time = millis();
	while ((millis() - start_time) < VAMP_ANSW_TIMEOUT && !bytes_read) {
		bytes_read = nrf_read();
	}

	wsn_radio.stopListening();

	return bytes_read; // Timeout - no respuesta
}

/* Enviar datos a un dispositivo (!!! aqui es donde habria que implementar el mecanismo de ACK !!!!) */
bool nrf_tell(uint8_t * dst_addr, uint8_t len) {

	wsn_radio.openWritingPipe(dst_addr);

	// Enviar datos
	if (!wsn_radio.write(nrf_buff, len)){
		return false;
	}
	return true; // Éxito al enviar datos		
}

/* Comunicación nRF24 */
uint8_t nrf_comm(uint8_t * dst_addr, uint8_t * data, uint8_t len) {

	/* Verificar que el chip está conectado */
	if (!wsn_radio.isChipConnected()) {
		#ifdef VAMP_DEBUG
		Serial.println("rf24 out");
		#endif /* VAMP_DEBUG */
		return 0;
	}

	if (!data) {
		return 0;
	}

	/* Mode RECEIVE (READ/listening nRF24) */
	if (!dst_addr) {

		uint8_t recv_len = 0;
		/* Si está en modo bajo consumo, hay que abrir una ventana de
			escucha. Esto exige un mecanismo de sincronización que no es
			parte de estas funciones */
		if(vamp_get_settings() & VAMP_RMODE_A){
			/* Encender el chip */
			wsn_radio.powerUp();
			/* ... abrir ventana de escucha */
			recv_len = nrf_listen_window();
			/* Apagar el chip */
			wsn_radio.powerDown();
		} else {
			// Modo siempre leer, no hay que abrir ventana
			recv_len = nrf_read();
		}

		/* Verificar que lo que se recibió cabe en el buffer */
		if (recv_len <= len) {
			/* Procesar el mensaje recibido */
			memcpy(data, nrf_buff, recv_len);
		}

		return recv_len; // Retornar el número de bytes leídos
	}

	/* Mode TELL */
	if (len && len <= VAMP_MAX_PAYLOAD_SIZE) {

		memcpy(nrf_buff, data, len);

		if(vamp_get_settings() & VAMP_RMODE_B) {
			/* Modo siempre escucha asi que que dejar de escuchar */
			wsn_radio.stopListening();
			/* Enviar datos */
			if (!nrf_tell(dst_addr, len)) {
				/* Si falló, se devuelve 0 */
				len = 0;
			}
			/* Volver a escuchar */
			wsn_radio.startListening();

			return len;

		}

		/* Modo bajo consumo, esperamos respuesta */
		if (vamp_get_settings() & VAMP_RMODE_A) {

			/* Encender el chip */
			wsn_radio.powerUp();
			if (nrf_tell(dst_addr, len)) {
				/* Si se envió correctamente, abrir ventana de escucha */
				len = nrf_listen_window();
				if (len) {
					memcpy(data, nrf_buff, len);
				}
			} else {
				/* Si hubo un error al enviar, pues se retorna un 0 */
				len = 0;
			}
			
			/* Apagar el chip */
			wsn_radio.powerDown();

			return len; // Retornar el número de bytes leídos
		}
	}
	return 0; //Aqui no se hizo nada porque no se cumplio ninguna de las condiciones
}


#endif /* NRF24L01 */