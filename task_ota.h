/*
   This code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#if !defined(__TASK_OTA__H__)
#define __TASK_OTA__H__

/**
 * @brief Do OTA process
 *
 * @param uri URI to firmware file
 * @param cert_pem Certificate
 */
void do_ota(const char *uri, const char *cert_pem);

#endif
