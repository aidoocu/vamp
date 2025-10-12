/** 
 * 
 * 
 * 
 */

 /** @brief Inicializa la WiFi station (STA) en el ESP8266
  * 
  * @param wifi_ssid SSID de la red WiFi
  * @param wifi_password Password de la red WiFi
  */
bool esp8266_sta_init(const char * wifi_ssid, const char * wifi_password);

/** @brief Configura el ip estática para la WiFi station (STA) en el ESP8266
 * 
 * @param ip Dirección IP estática
 * @param gateway Dirección IP del gateway
 * @param subnet Máscara de subred
 * @param dns1 Dirección IP del DNS primario
 * @param dns2 Dirección IP del DNS secundario
 */
void esp8266_sta_static_ip(IPAddress ip, IPAddress gateway, IPAddress subnet, IPAddress dns1, IPAddress dns2);

/** @brief Realiza una solicitud HTTP/HTTPS
 * 
 * @param profile Perfil de comunicación
 * @param data Datos a enviar
 * @param data_size Tamaño de los datos
 * @return Tamaño de los datos recibidos, 0 en caso de error
 */
size_t esp8266_http_request(const vamp_profile_t * profile, char * data, size_t data_size);