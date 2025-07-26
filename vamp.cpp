/** @file vamp.h
 * @brief VAMP Gateway header file
 *
 * This file contains definitions and function declarations for the VAMP Gateway.
 */

#include "vamp.h"
#include "vamp_gw.h"
#include "vamp_client.h"

/* -------------------------------------- Gateway -------------------------------------- */

void vamp_gw_init(vamp_internet_callback_t vamp_internet_callback, vamp_wsn_callback_t vamp_wsn_callback, const char * vamp_vreg_url, const char * vamp_gw_id) {

    /* Se inicializa el gateway local */
    vamp_local_gw_init(vamp_internet_callback, vamp_wsn_callback, vamp_vreg_url, vamp_gw_id);

    /* Inicializar la tabla VAMP */
    vamp_table_update();
}

void vamp_gw_sync(void) {

    /* Synchronize VAMP Gateway with VREG */
    vamp_table_update();

    /* Detect expired VAMP devices */
    vamp_detect_expired();
}

/* -------------------------------------- Client -------------------------------------- */

void vamp_client_init(const uint8_t * vamp_client_id, vamp_wsn_callback_t wsn_comm_callback) {

    /* Se inicializa el cliente VAMP */
    vamp_local_client_init(vamp_client_id, wsn_comm_callback);

}

/* -------------------------------------- WSN -------------------------------------- */

void vamp_gw_wsn(void) {

    vamp_get_wsn();

}


bool vamp_wsn_send(uint8_t * data, uint8_t data_len) {
    return vamp_send_data(data, data_len);
}


/** ????? @todo hay que definir algun elemento que no sea correcto como una direccion nula
 00:00:00:00:00 o de broadcast o ... */
bool vamp_is_rf_id_valid(const uint8_t * rf_id) {
	// Verificar que el RF_ID no sea NULL y tenga la longitud correcta
	if (rf_id == NULL) {
		return false;
	}
	
	if (rf_id[0] == 0 && rf_id[1] == 0 && rf_id[2] == 0 &&
	    rf_id[3] == 0 && rf_id[4] == 0) {
		return false; // RF_ID nulo
	}

	if (rf_id[0] == 0xFF && rf_id[1] == 0xFF && rf_id[2] == 0xFF &&
	    rf_id[3] == 0xFF && rf_id[4] == 0xFF) {
		return false; // RF_ID de broadcast
	}

	return true;
}