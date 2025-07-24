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

/* -------------------------------------- WSN -------------------------------------- */

void vamp_ask_wsn(void) {

    //uint8_t wsn_buffer[VAMP_MAX_PAYLOAD_SIZE]; // Buffer para datos NRF24L01

    vamp_get_wsn();

}