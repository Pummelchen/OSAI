#ifndef SFTP_SERVER_H
#define SFTP_SERVER_H

#include <xaios/types.h>

/* SFTP protocol handler */
int sftp_handle_message(int sockfd, const uint8_t *data, uint32_t len);
int sftp_session_start(int sockfd, uint32_t channel_id);

#endif
