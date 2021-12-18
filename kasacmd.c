/* kasacmd - A simple program to control a TP-Link Kasa device
 *
 * Copyright 2020, Pascal Martin
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 *
 * kasacmd.c - control a TP-Link Kasa device
 *
 * SYNOPSYS:
 *
 * kasacmd
 * kasacmd <host>
 * kasacmd <host> alias <name>
 * kasacmd <host> on [<model> [<outlet>]]
 * kasacmd <host> off [<model> [<outlet>]]
 *
 * Supported commands are: 
 *    alias: change the alias name configured in the device.
 *    on:    turn the device on.
 *    off:   turn the device off.
 *
 * With no parameter specified, the program send a broadcast to discover all
 * devices present, and prints system information from all responding devices.
 *
 * With no command specified, the program requests and prints system
 * information for the specified device only.
 *
 * In both cases the output is the raw message from the device. No formatting.
 *
 * The supported models are:
 *    kp400 (dual outlet: outlet ID is required)
 *    hs220 (single outlet: outlet ID ignored)
 *
 * If the model is not specified, the program uses the most commonly supported
 * variant of the protocol. It might not always work.
 */

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <arpa/inet.h>

static int KasaPort = 9999;
static int KasaSocket = -1;
static struct sockaddr_in KasaAddress;

static void kasacmd_socket (void) {

    KasaAddress.sin_family = AF_INET;
    KasaAddress.sin_port = htons(KasaPort);
    KasaAddress.sin_addr.s_addr = INADDR_BROADCAST;

    KasaSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (KasaSocket < 0) {
        printf ("cannot open UDP socket: %s\n", strerror(errno));
        exit(1);
    }

    int value = 1;
    if (setsockopt(KasaSocket, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value)) < 0) {
        printf ("cannot broadcast: %s\n", strerror(errno));
        exit(1);
    }
    printf ("UDP socket is ready.\n");
}

static unsigned char hex2bin(char data) {
    if (data >= '0' && data <= '9')
        return data - '0';
    if (data >= 'a' && data <= 'f')
        return data - 'a' + 10;
    if (data >= 'A' && data <= 'F')
        return data - 'A' + 10;
    return 0;
}

static char bin2hex (unsigned char d) {
    d &= 0x0f;
    if (d >= 0 && d <= 9) return '0' + d;
    if (d >= 10 && d <= 15) return ('a' - 10) + d;
    return '0';
}

static void kasacmd_send (const char *data) {

    char encoded[1024];
    int i;
    char key = 0xab;
    int length = strlen(data);
    if (length > sizeof(encoded)) {
        printf ("Data too large to encode: %d is greater than %d\n",
                length, sizeof(encoded));
        exit(1);
    }
    for (i = 0; i < length; ++i) {
        key = encoded[i] = key ^ data[i];
    }

    printf ("Sending %s\n", data);
    int sent = sendto (KasaSocket, encoded, length, 0,
                       (struct sockaddr *)(&KasaAddress),
                       sizeof(struct sockaddr_in));
    if (sent < 0) {
        printf ("** sendto() error: %s\n", strerror(errno));
        exit(1);
    }
}

static int kasacmd_wait (void) {

    fd_set receive;
    struct timeval timeout;

    FD_ZERO(&receive);
    FD_SET(KasaSocket, &receive);

    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    return select (KasaSocket+1, &receive, 0, 0, &timeout);
}

static void kasacmd_receive (void) {

    char encoded[1024];
    char data[1025];
    char source[100];
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);

    int size = recvfrom (KasaSocket, encoded, sizeof(encoded), 0,
                         (struct sockaddr *)(&addr), &addrlen);

    if (size <= 0) {
        printf ("** recvfrom() error: %s\n", strerror(errno));
        return;
    }
    int i;
    char key = 0xab;
    for (i = 0; i < size; ++i) {
        data[i] = key ^ encoded[i];
        key = encoded[i];
    }
    data[i] = 0;
    int ip = htonl(addr.sin_addr.s_addr);
    printf ("Received from %d.%d.%d.%d: %s\n",
            0xff & (ip >> 24), 0xff & (ip >> 16), 0xff & (ip >> 8), 0xff & ip,
            data);
}

static int kasacmd_resolve (const char *host) {

    int result = 0;
    struct addrinfo hints;
    struct addrinfo *resolved;
    struct addrinfo *cursor;
    hints.ai_flags = AI_ADDRCONFIG;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo (host, 0, &hints, &resolved)) return 0;

    for (cursor = resolved; cursor; cursor = cursor->ai_next) {
        if (cursor->ai_family != AF_INET) continue;
        memcpy (&KasaAddress, cursor->ai_addr, sizeof(KasaAddress));
        KasaAddress.sin_port = htons(KasaPort);
        result = 1;
    }
    freeaddrinfo(resolved);
    return result;
}

int main (int argc, char **argv) {

    const char *host = 0;
    const char *cmd = 0;
    const char *model = 0;
    const char *id = 0;

    char buffer[256];

    if (argc >= 2) {
       host = argv[1];
       if (argc >= 3) {
           cmd = argv[2];
           if (argc >= 4) {
               model = argv[3];
               if (argc >= 5) {
                   id = argv[4];
               }
           }
       }
    }
    
    kasacmd_socket ();
    if (host) {
        if (!kasacmd_resolve (host)) {
            printf ("Cannot resolve %s\n", host);
            exit(1);
        }
    }

    if (!cmd) {
        kasacmd_send ("{\"system\":{\"get_sysinfo\":{}}}");
    } else if (!strcmp (cmd, "alias")) {
        if (model) {
            char buffer[200];
            snprintf (buffer, sizeof(buffer),
                      "{\"system\":{\"set_dev_alias\":{\"alias\":\"%s\"}}}",
                      model);
            kasacmd_send (buffer);
        }
    } else if (!strcmp (cmd, "on")) {
        if (!model) {
            kasacmd_send ("{\"system\":{\"set_relay_state\":{\"state\":1}}}");
        } else if (!strcmp (model, "kp400")) {
            if (!id) {
                printf ("Outlet ID is required\n");
                exit (1);
            }
            char buffer[200];
            snprintf (buffer, sizeof(buffer),
                      "{\"context\":{\"child_ids\":[\"%s\"]},\"system\":{\"set_relay_state\":{\"state\":1}}}", id);
            kasacmd_send (buffer);
        } else if (!strcmp (model, "hs220")) {
            kasacmd_send ("{\"smartlife.iot.dimmer\":{\"set_switch_state\":{\"state\":1}}}");
        }
    } else if (!strcmp (cmd, "off")) {
        if (!model) {
            kasacmd_send ("{\"system\":{\"set_relay_state\":{\"state\":0}}}");
        } else if (!strcmp (model, "kp400")) {
            if (!id) {
                printf ("Outlet ID is required\n");
                exit (1);
            }
            char buffer[200];
            snprintf (buffer, sizeof(buffer),
                      "{\"context\":{\"child_ids\":[\"%s\"]},\"system\":{\"set_relay_state\":{\"state\":0}}}", id);
            kasacmd_send (buffer);
        } else if (!strcmp (model, "hs220")) {
            kasacmd_send ("{\"smartlife.iot.dimmer\":{\"set_switch_state\":{\"state\":0}}}");
        }
    }
    while (kasacmd_wait() > 0) kasacmd_receive();
    return 0;
}

