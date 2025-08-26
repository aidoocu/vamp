/**
 * 
 * 
 * 
 * 
 */

#ifndef _VAMP_CLIENT_H_
#define _VAMP_CLIENT_H_

#include <Arduino.h>

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

/** @brief Send data with VAMP using a profile by default (0)
 * 
 *  @param data Pointer to the data to be sent
 *  @param len Length of the data to be sent
 *  @return  0 on failure, 
 *          0 < return <= VAMP_MAX_PAYLOAD_SIZE data contained a msg from the gateway
 *          > VAMP_MAX_PAYLOAD_SIZE (VAMP_MAX_PAYLOAD_SIZE + 1) success        
 */
uint8_t vamp_client_tell(const uint8_t * data, uint8_t len);

/** @overload Send data with VAMP using a specific profile
 *  @param data Pointer to the data to be sent
 *  @param len Length of the data to be sent
 *  @param profile The profile to be used for sending the data
 */
uint8_t vamp_client_tell(const uint8_t profile, const uint8_t * data, uint8_t len);

/** @brief Ask for data with VAMP using a specific profile
 *  @param profile The profile to be used for asking the data
 *  @return true if the request was successful, false otherwise
 *  @note   This function makes a TELL request with the form
 * *        data[1] = { '\0' } or a empty string. This is because 
 * *        the target profile is assumed to be a GET, which will 
 * *        ignore the payload data.
 * *        This function does not expect a response from the gateway, 
 * *        only an TICKET, so it should be followed by the poll to know 
 * *        if there are messages returning from the endpoint via gateway.
 */
bool vamp_client_ask(uint8_t profile);

/** @overload Ask for data with VAMP using a default profile
 *  @return true if the request was successful, false otherwise
 */
bool vamp_client_ask(void);

/** @brief Polling gateway asking for data using VAMP
 * 
 * @param data Pointer to the buffer to store received data
 * @param len Length of the buffer
 * @return  0 on failure, 
 *          0 < return <= VAMP_MAX_PAYLOAD_SIZE data contained a msg from the gateway
 */
uint8_t vamp_client_poll(uint16_t ticket, uint8_t * data, uint8_t len);

#endif // _VAMP_CLIENT_H_
