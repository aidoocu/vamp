/**
 * 
 * 
 * 
 * 
 */

#ifndef _VAMP_CLIENT_H_
#define _VAMP_CLIENT_H_

#include "vamp.h"

/**
 * @brief Initialize VAMP client with VREG URL and gateway ID
 * 
 * This function initializes the VAMP client with the specified VREG URL and gateway ID.
 * It is used to set up the client for communication with the VREG server.
 * 
 * @param vamp_client_id Pointer to the VAMP client ID (RF_ID) to be used for communication.
 */
void vamp_client_init(uint8_t * vamp_client_id);

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
 * @return  0 on failure, 
 *          0 < return <= VAMP_MAX_PAYLOAD_SIZE data contained a msg from the gateway
 *          > VAMP_MAX_PAYLOAD_SIZE (VAMP_MAX_PAYLOAD_SIZE + 1) success (just ACK)          
 */
uint8_t vamp_client_tell(const uint8_t * data, uint8_t len);

/** @brief Ask for data using VAMP in asynchronous mode.
 *          this function acts as GET but without waiting for a response, the gateway
 *          will receive a special data type frame with len = 0, but with a '\0' terminator
 *          in the first byte of payload:
 *          | 0x00 | VID | '\0' ... 
 *          and the gateway will make a GET to the registered resource and will send the 
 *          response back using a poll or the next node contact.
 * @return true if the ask was successful, false otherwise
 */
bool vamp_client_ask(void);

/** @brief Polling gateway asking for data using VAMP
 * 
 * @param data Pointer to the buffer to store received data
 * @param len Length of the buffer
 * @return  0 on failure, 
 *          0 < return <= VAMP_MAX_PAYLOAD_SIZE data contained a msg from the gateway
 */
uint8_t vamp_client_poll(uint8_t * data, uint8_t len);

#endif // _VAMP_CLIENT_H_
