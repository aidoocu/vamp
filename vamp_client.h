/**
 * 
 * 
 * 
 * 
 */

#ifndef _VAMP_CLIENT_H_
#define _VAMP_CLIENT_H_

#include "vamp.h"


/** @brief Initialize local VAMP client
 * 
 * @param vamp_client_id Pointer to the VAMP client ID
 */
void vamp_local_client_init(const uint8_t * vamp_client_id);


// Si se se quiere validar si un mensaje es de tipo VAMP
//bool is_vamp_message(const uint8_t *data, size_t length);

/** @brief Check if VAMP is joined
 * 
 * @return true if joined, false otherwise
 */
bool vamp_is_joined(void);

/** @brief Join Network
 * 
 */
bool vamp_join_network(void);

/** @brief Force rejoin to the network
 * 
 * This function resets the connection with the gateway and attempts to rejoin the network.
 * It is useful in cases where the connection is lost or needs to be re-established.
 * 
 * @return true if rejoin was successful, false otherwise
 */
bool vamp_force_rejoin(void);

/** @brief Send data using VAMP
 * 
 * @param data Pointer to the data to be sent
 * @param len Length of the data to be sent
 * @return bool true on success, false on failure
 */
bool vamp_send_data(const uint8_t * data, uint8_t len);


#endif // _VAMP_CLIENT_H_
