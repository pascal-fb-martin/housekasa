/* HouseKasa - A simple home web server for control of TP-Link Kasa Devices
 *
 * Copyright 2021, Pascal Martin
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
 * housekasa_device.c - Control a TP-Link Kasa Device.
 *
 * SYNOPSYS:
 *
 * const char *housekasa_device_initialize (int argc, const char **argv);
 *
 *    Initialize this module at startup.
 *
 * int housekasa_device_changed (void);
 *
 *    Indicate if the configuration was changed due to discovery, which
 *    means it must be saved.
 *
 * const char *housekasa_device_live_config (char *buffer, int size);
 *
 *    Recover the current live config, typically to save it to disk after
 *    a change has been detected.
 *
 * const char *housekasa_device_refresh (void);
 *
 *    Re-evaluate the configuration after it changed.
 *
 * int housekasa_device_count (void);
 *
 *    Return the number of configured devices available.
 *
 * const char *housekasa_device_name (int point);
 *
 *    Return the name of a kasa device.
 *
 * const char *housekasa_device_failure (int point);
 *
 *    Return a string describing the failure, or a null pointer if healthy.
 *
 * int    housekasa_device_commanded (int point);
 * time_t housekasa_device_deadline (int point);
 *
 *    Return the last commanded state, or the command deadline, for
 *    the specified kasa device.
 *
 * int housekasa_device_get (int point);
 *
 *    Get the actual state of the device.
 *
 * int housekasa_device_set (int point, int state, int pulse);
 *
 *    Set the specified point to the on (1) or off (0) state for the pulse
 *    length specified. The pulse length is in seconds. If pulse is 0, the
 *    device is maintained to the requested state until a new state is issued.
 *
 *    Return 1 on success, 0 if the device is not known and -1 on error.
 *
 * void housekasa_device_periodic (void);
 *
 *    This function must be called every second. It runs the Kasa device
 *    discovery and ends the expired pulses.
 */

#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <net/if.h>
#include <ifaddrs.h>
#include <netpacket/packet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "echttp.h"
#include "echttp_json.h"
#include "houselog.h"
#include "houseconfig.h"

#include "housekasa_device.h"


// This offset is used to "sign" an ID that contains a device index.
// This is more for debug _and simulation_ purpose: this module does
// not uses the value of the ID from received messages. A simulator
// might however use it to identify the context.
//
#define WIZ_ID_OFFSET 12000

struct DeviceMap {
    char *name;
    char *model;
    char *id;
    char *child;
    char *description;
    struct sockaddr_in ipaddress;
    time_t detected;
    int status;
    int commanded;
    time_t pending;
    time_t deadline;
    time_t last_sense;
};

static int DeviceListChanged = 0;

static struct DeviceMap *Devices;
static int DevicesCount = 0;
static int DevicesSpace = 0;

static int KasaDevicePort = 9999;
static int KasaSocket = -1;

#define KASASENSEMAX 64

struct NetworkMap {
    char *name;
    struct sockaddr_in addr;
};
static int KasaSenseCount = 0;
static struct NetworkMap KasaSense[KASASENSEMAX];


int housekasa_device_count (void) {
    return DevicesCount;
}

int housekasa_device_changed (void) {
    if (DeviceListChanged) {
        DeviceListChanged = 0;
        return 1;
    }
    return 0;
}

const char *housekasa_device_name (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].name;
}

int housekasa_device_commanded (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].commanded;
}

time_t housekasa_device_deadline (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].deadline;
}

const char *housekasa_device_failure (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    if (!Devices[point].detected) return "silent";
    return 0;
}

int housekasa_device_get (int point) {
    if (point < 0 || point > DevicesCount) return 0;
    return Devices[point].status;
}

static int housekasa_device_id_search (const char *id, const char *child) {
    int i;
    for (i = 0; i < DevicesCount; ++i) {
        if (strcasecmp(id, Devices[i].id)) continue;
        if (!Devices[i].child) {
            if (!child) return i; // Single outlet device.
        } else {
            if (!strcasecmp(child, Devices[i].child)) return i;
        }
    }
    return -1;
}

static int housekasa_device_address_search (struct sockaddr_in *addr) {
    int i;
    for (i = 0; i < DevicesCount; ++i) {
        if (addr->sin_addr.s_addr == Devices[i].ipaddress.sin_addr.s_addr)
            return i;
    }
    return -1;
}

static void housekasa_device_socket (void) {

    KasaSense[0].name = 0;
    KasaSense[0].addr.sin_family = AF_INET;
    KasaSense[0].addr.sin_port = htons(KasaDevicePort);
    KasaSense[0].addr.sin_addr.s_addr = INADDR_BROADCAST;
    KasaSenseCount = 1;

    KasaSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (KasaSocket < 0) {
        houselog_trace (HOUSE_FAILURE, "DEVICE",
                        "cannot open UDP socket: %s", strerror(errno));
        exit(1);
    }

    int value = 1;
    if (setsockopt(KasaSocket, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value)) < 0) {
        houselog_trace (HOUSE_FAILURE, "SOCKET",
                        "cannot broadcast: %s", strerror(errno));
        exit(1);
    }

    houselog_trace (HOUSE_INFO, "DEVICE", "UDP port %d is now open", KasaDevicePort);
}

static void housekasa_device_send (const struct sockaddr_in *a, const char *d) {
    if (echttp_isdebug()) {
        long ip = ntohl((long)(a->sin_addr.s_addr));
        int port = ntohs(a->sin_port);
        printf ("Sending packet to %d.%d.%d.%d(port %d): %s\n",
                (ip>>24)&0xff, (ip>>16)&0xff, (ip>>8)&0xff, ip&0xff, port, d);
    }
    int i;
    char encoded[1500];
    char key = 0xab;
    int length = strlen(d);
    if (length > sizeof(encoded)) {
        houselog_trace (HOUSE_FAILURE, "INTERNAL",
                        "Encoding buffer too small: has %d, needs %d",
                        sizeof(encoded), length);
        return;
    }
    for (i = 0; i < length; ++i) {
        key = encoded[i] = key ^ d[i];
    }
    int sent = sendto (KasaSocket, encoded, length, 0,
                       (struct sockaddr *)a, sizeof(struct sockaddr_in));
    if (sent < 0)
        houselog_trace
            (HOUSE_FAILURE, "DEVICE", "sendto() error: %s", strerror(errno));
}

static void housekasa_device_sense (const struct sockaddr_in *a) {
    housekasa_device_send (a, "{\"system\":{\"get_sysinfo\":{}}}");
}

static void housekasa_device_control (int device, int state) {
    char buffer[256];
    if (Devices[device].child && Devices[device].child[0]) {
        snprintf (buffer, sizeof(buffer),
              "{\"context\":{\"child_ids\":[\"%s%s\"]},\"system\":{\"set_relay_state\":{\"state\":%c}}}",
              Devices[device].id, Devices[device].child, state?'1':'0');
    } else {
        snprintf (buffer, sizeof(buffer),
              "{\"system\":{\"set_relay_state\":{\"state\":%c}}}",
              state?'1':'0');
    }
    housekasa_device_send (&(Devices[device].ipaddress), buffer);
}

int housekasa_device_set (int device, int state, int pulse) {

    const char *namedstate = state?"on":"off";
    time_t now = time(0);

    if (device < 0 || device > DevicesCount) return 0;

    if (echttp_isdebug()) {
        if (pulse) fprintf (stderr, "set %s to %s at %ld (pulse %ds)\n", Devices[device].name, namedstate, now, pulse);
        else       fprintf (stderr, "set %s to %s at %ld\n", Devices[device].name, namedstate, now);
    }

    if (pulse > 0) {
        Devices[device].deadline = now + pulse;
        houselog_event ("DEVICE", Devices[device].name, "SET",
                        "%s FOR %d SECONDS", namedstate, pulse);
    } else {
        Devices[device].deadline = 0;
        houselog_event ("DEVICE", Devices[device].name, "SET", "%s", namedstate);
    }
    Devices[device].commanded = state;
    Devices[device].pending = now + 5;

    // Only send a command if we detected the device on the network.
    //
    if (Devices[device].detected) {
        housekasa_device_control (device, state);
    }
}

static void housekasa_device_reset (int i, int status) {
    Devices[i].commanded = Devices[i].status = status;
    Devices[i].pending = Devices[i].deadline = 0;
}

void housekasa_device_periodic (time_t now) {

    static time_t LastRetry = 0;
    static time_t LastSense = 0;
    int i;

    if (now >= LastSense + 60) {
        for (i = 0; i < KasaSenseCount; ++i)
            housekasa_device_sense(&(KasaSense[i].addr));
        LastSense = now;
    }

    if (now < LastRetry + 5) return;
    LastRetry = now;

    for (i = 0; i < DevicesCount; ++i) {

        if (now >= Devices[i].last_sense + 35) {
            if (Devices[i].ipaddress.sin_addr.s_addr != 0)
                housekasa_device_sense(&(Devices[i].ipaddress));
            Devices[i].last_sense = now;
        }

        // If we did not detect a device for 3 senses, consider it failed.
        if (Devices[i].detected > 0 && Devices[i].detected < now - 100) {
            houselog_event ("DEVICE", Devices[i].name, "SILENT",
                            "ADDRESS %s",
                            inet_ntoa(Devices[i].ipaddress.sin_addr));
            housekasa_device_reset (i, 0);
            Devices[i].detected = 0;
        }

        if (Devices[i].deadline > 0 && now >= Devices[i].deadline) {
            houselog_event ("DEVICE", Devices[i].name, "RESET", "END OF PULSE");
            Devices[i].commanded = 0;
            Devices[i].pending = now + 5;
            Devices[i].deadline = 0;
        }
        if (Devices[i].status != Devices[i].commanded) {
            if (Devices[i].pending > now) {
                if (Devices[i].detected) {
                    const char *state = Devices[i].commanded?"on":"off";
                    houselog_event ("DEVICE", Devices[i].name, "RETRY", state);
                    housekasa_device_control (i, Devices[i].commanded);
                }
            } else {
                if (Devices[i].pending)
                    houselog_event ("DEVICE", Devices[i].name, "TIMEOUT", "");
                housekasa_device_reset (i, Devices[i].status);
            }
        }
    }
}

static void housekasa_device_refresh_string (char **store, const char *value) {
    if (value) {
        if (*store) {
            if (! strcmp (*store, value)) return; // No change needed
            free (*store);
        }
        *store = strdup(value);
    } else {
        if (*store) (*store)[0] = 0;
    }
}

static int housekasa_device_add (const char *model,
                                 const char *id, const char *child) {
    if (DevicesCount < DevicesSpace) {
        int i = DevicesCount++;
        Devices[i].id = strdup (id);
        Devices[i].model = model?strdup (model):0;
        if (child)
            Devices[i].child = strdup(child);
        housekasa_device_reset (i, 0);
        Devices[i].last_sense = 0;
        return i;
    }
    houselog_trace (HOUSE_FAILURE,
                    "DEVICE", "no space for device %s", id);
}

static int housekasa_device_gethost (const char *name, struct sockaddr_in *a) {
    struct addrinfo hints;
    struct addrinfo *resolved;

    hints.ai_flags = 0;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    if (getaddrinfo (name, 0, &hints, &resolved)) return 0;
    if (resolved) {
        a->sin_addr.s_addr =
            ((struct sockaddr_in *)(resolved->ai_addr))->sin_addr.s_addr;
        freeaddrinfo (resolved);
        return 1;
    }
    return 0;
}

const char *housekasa_device_refresh (void) {

    int i;
    int devices;
    int requested;
    int oldconfig = DevicesCount;

    for (i = 0; i < DevicesCount; ++i) {
        Devices[i].detected = 0;
        Devices[i].deadline = 0;
        Devices[i].pending = 0;
    }
    DevicesCount = 0;

    if (houseconfig_size() > 0) {
        devices = houseconfig_array (0, ".kasa.devices");
        if (devices < 0) return "cannot find devices array";

        requested = houseconfig_array_length (devices);
        if (echttp_isdebug()) fprintf (stderr, "found %d devices\n", requested);
    }
    int needed = requested + 32;

    // Allocate, or reallocate without loosing what we already have.
    //
    if (needed > DevicesSpace) {
        Devices = realloc (Devices, needed * sizeof(struct DeviceMap));
        memset (Devices+DevicesSpace, 0,
                (needed - DevicesSpace) * sizeof(struct DeviceMap));
        DevicesSpace = needed;
    }
    if (!Devices) return "no more memory";

    for (i = 0; i < requested; ++i) {
        int device = houseconfig_array_object (devices, i);
        if (device <= 0) continue;
        const char *model = houseconfig_string (device, ".model");
        const char *id = houseconfig_string (device, ".id");
        if (!id) continue;
        const char *child = houseconfig_string (device, ".child");
        int idx = housekasa_device_id_search (id, child);
        if (idx >= 0) continue; // Duplicate?
        idx = housekasa_device_add (model, id, child);
        housekasa_device_refresh_string (&(Devices[idx].name),
                                         houseconfig_string (device, ".name"));
        housekasa_device_refresh_string (&(Devices[idx].id),
                                         houseconfig_string (device, ".id"));
        housekasa_device_refresh_string (&(Devices[idx].child),
                                         houseconfig_string (device, ".child"));
        housekasa_device_refresh_string (&(Devices[idx].description),
                                         houseconfig_string (device, ".description"));
        if (echttp_isdebug()) fprintf (stderr, "load device %s, ID %s%s\n", Devices[idx].name, Devices[idx].id, Devices[i].child);
        housekasa_device_reset (idx, Devices[idx].status);
    }

    devices = houseconfig_array (0, ".kasa.net");
    if (devices < 0) return "cannot find network array";
    requested = houseconfig_array_length (devices);
    if (echttp_isdebug()) fprintf (stderr, "found %d networks\n", requested);
    if (requested >= KASASENSEMAX) requested = KASASENSEMAX - 1;

    for (i = 0; i < KasaSenseCount; ++i) {
        if (KasaSense[i].name) free(KasaSense[i].name);
        KasaSense[i].name = 0;
    }
    KasaSenseCount = 1;

    for (i = 0; i < requested; ++i) {
        KasaSense[KasaSenseCount] = KasaSense[0];
        char index[10];
        snprintf (index, sizeof(index), "[%d]", i);
        const char *addr = houseconfig_string(devices, index);
        if ((!addr) || (addr[0] == 0)) continue;
        if (echttp_isdebug())
            fprintf (stderr, "load broadcast IP address %s\n", addr);
        if (!housekasa_device_gethost(addr,&(KasaSense[KasaSenseCount].addr))) {
            if (echttp_isdebug())
                fprintf (stderr, "invalid address %s\n", addr);
        } else {
            houselog_event ("NETWORK", addr, "ADDED", "AS %s",
                            inet_ntoa(KasaSense[KasaSenseCount].addr.sin_addr));
            KasaSense[KasaSenseCount].name = strdup(addr);
            KasaSenseCount += 1;
            if (KasaSenseCount >= KASASENSEMAX) break;
        }
    }
    return 0;
}

const char *housekasa_device_live_config (char *buffer, int size) {

    static char pool[65537];
    static ParserToken token[1024];
    ParserContext context = echttp_json_start (token, 1024, pool, sizeof(pool));

    int i;

    int root = echttp_json_add_object (context, 0, 0);
    int top  = echttp_json_add_object (context, root, "kasa");
    int items = echttp_json_add_array (context, top, "devices");

    for (i = 0; i < DevicesCount; ++i) {
        int device = echttp_json_add_object (context, items, 0);
        if (Devices[i].name && Devices[i].name[0])
            echttp_json_add_string (context, device, "name", Devices[i].name);
        if (Devices[i].ipaddress.sin_addr.s_addr)
            echttp_json_add_string
                (context, device, "ip", 
                 inet_ntoa(Devices[i].ipaddress.sin_addr));
        if (Devices[i].model && Devices[i].model[0])
            echttp_json_add_string (context, device, "model", Devices[i].model);
        if (Devices[i].id && Devices[i].id[0])
            echttp_json_add_string (context, device, "id", Devices[i].id);
        if (Devices[i].child && Devices[i].child[0])
            echttp_json_add_string (context, device, "child", Devices[i].child);
        if (Devices[i].description && Devices[i].description[0])
            echttp_json_add_string (context, device, "description", Devices[i].description);
    }

    if (KasaSenseCount > 1) {
        items = echttp_json_add_array (context, top, "net");
        for (i = 1; i < KasaSenseCount; ++i) {
            if (KasaSense[i].name && KasaSense[i].name[0])
                echttp_json_add_string (context, items, 0, KasaSense[i].name);
        }
    }
    return echttp_json_export (context, buffer, size);
}

static void housekasa_device_status_update (int device, int status) {
    if (device < 0) return;
    if (!Devices[device].detected)
        houselog_event ("DEVICE", Devices[device].name,
            "DETECTED", "ADDRESS %s",
            inet_ntoa(Devices[device].ipaddress.sin_addr));
    if (status != Devices[device].status) {
        if (Devices[device].pending &&
                (status == Devices[device].commanded)) {
            houselog_event ("DEVICE", Devices[device].name,
                            "CONFIRMED", "FROM %s TO %s",
                            Devices[device].status?"on":"off",
                            status?"on":"off");
            Devices[device].pending = 0;
        } else {
            houselog_event ("DEVICE", Devices[device].name,
                            "CHANGED", "FROM %s TO %s",
                            Devices[device].status?"on":"off",
                            status?"on":"off");
            // Device commanded by someone else.
            Devices[device].commanded = status;
            Devices[device].pending = 0;
        }
        Devices[device].status = status;
    }
    Devices[device].detected = time(0);
}

static const char *housekasa_device_json_string (ParserToken *json,
                                                 int parent,
                                                 const char *path) {
    if (parent < 0) return 0;
    int i = echttp_json_search(json+parent, path);
    if (i < 0 || json[parent+i].type != PARSER_STRING) return 0;
    return json[parent+i].value.string;
}

static int housekasa_device_json_integer (ParserToken *json,
                                          int parent,
                                          const char *path) {
    if (parent < 0) return 0;
    int i = echttp_json_search(json+parent, path);
    if (i < 0 || json[parent+i].type != PARSER_INTEGER) return 0;
    return json[parent+i].value.integer;
}

static int housekasa_device_json_array (ParserToken *json,
                                        int parent,
                                        const char *path) {
    if (parent < 0) return -1;
    int i = echttp_json_search(json+parent, path);
    if (i < 0 || json[parent+i].type != PARSER_ARRAY) return -1;
    return parent+i;
}

int housekasa_device_json_array_object (ParserToken *json,
                                        int parent,
                                        int index) {
    char path[128];
    snprintf (path, sizeof(path), "[%d]", index);
    int i = echttp_json_search (json+parent, path);
    if (i < 0 || json[parent+i].type != PARSER_OBJECT) return -1;
    return parent+i;
}

static void housekasa_device_getinfo (ParserToken *json, int count,
                                      struct sockaddr_in *addr,
                                      const char *data) {

    int device;
    time_t now = time(0);

    // Retrieve ID and current device state from the JSON data.
    //
    const char *id =
        housekasa_device_json_string (json, 0, ".system.get_sysinfo.deviceId");
    if (!id) {
        houselog_trace (HOUSE_FAILURE,
                        "DEVICE", "no valid device ID in: %s", data);
        return;
    }
    const char *model =
        housekasa_device_json_string (json, 0, ".system.get_sysinfo.model");
    if (!model) model = "(unknown)";

    if (echttp_isdebug()) fprintf (stderr, "Device model %s: %s\n", model, id);
    int children =
        housekasa_device_json_array (json, 0, ".system.get_sysinfo.children");
    if (children >= 0) {
        const char *parent = id;
        // TBD: enumerate all entries in array .system.get_sysinfo.children
        //
        int i;
        int end = json[children].length;
        for (i = 0; i < end; ++i) {
            int child = housekasa_device_json_array_object (json, children, i);
            id = housekasa_device_json_string (json, child, ".id");
            if (!id) continue;
            if (echttp_isdebug()) fprintf (stderr, "Child plug %s\n", id);
            device = housekasa_device_id_search (parent, id);
            if (device < 0 && DevicesCount < DevicesSpace) {
                device = housekasa_device_add (model, parent, id);
                if (device < 0) return;
                housekasa_device_refresh_string
                    (&(Devices[device].name),
                     housekasa_device_json_string (json, child, ".alias"));
                houselog_event ("DEVICE", Devices[device].name, "DISCOVERED",
                                "ADDRESS %s (CHILD %s)",
                                inet_ntoa(addr->sin_addr), id);
                DeviceListChanged = 1;
                if (echttp_isdebug())
                     fprintf (stderr, "Device %s %s added\n", parent, id);
                Devices[device].detected = now; // No "detected" event.
            }
            if (device >= 0) {
                if (echttp_isdebug())
                    fprintf (stderr, "Child plug %s (device %s)\n", id, Devices[device].name);
                Devices[device].ipaddress = *addr; // Keep latest address.
                if (!Devices[device].model)
                    Devices[device].model = strdup(model);
            }
            housekasa_device_status_update
                (device, housekasa_device_json_integer (json, child, ".state"));
        }
    } else {
        device = housekasa_device_id_search (id, 0);
        if (device < 0 && DevicesCount < DevicesSpace) {
            device = housekasa_device_add (model, id, 0);
            if (device >= 0) {
                housekasa_device_refresh_string
                    (&(Devices[device].name),
                     housekasa_device_json_string(json, 0, ".system.get_sysinfo.alias"));
                houselog_event ("DEVICE", Devices[device].name, "DISCOVERED",
                                "ADDRESS %s",
                                inet_ntoa(addr->sin_addr));
                DeviceListChanged = 1;
                if (echttp_isdebug())
                     fprintf (stderr, "Device %s added\n", id);
            }
        }
        if (device > 0) {
            Devices[device].ipaddress = *addr; // Keep latest address.
            if (!Devices[device].model)
                Devices[device].model = strdup(model);
        }
        housekasa_device_status_update
            (device,
             housekasa_device_json_integer
                 (json, 0, ".system.get_sysinfo.relay_state"));
    }
}

static void housekasa_device_response (ParserToken *json, int count,
                                       struct sockaddr_in *addr,
                                       const char *data) {

    int result = echttp_json_search (json, ".system.set_relay_state.err_code");
    if (result >= 0) {
        if (json[result].value.integer) return; // Error.

        int device = housekasa_device_address_search (addr);
        int i;
        for (i = 0; i < DevicesCount; ++i) {
            if (addr->sin_addr.s_addr != Devices[i].ipaddress.sin_addr.s_addr)
                continue;

            // The response does not include the current state of the device.
            // If this is a multi-plug device, we don't know which child
            // this is about.
            // The easiest is just to query the complete device state now.
            //
            housekasa_device_sense(&(Devices[i].ipaddress));
            Devices[i].last_sense = time(0);

            break; // No point in requesting for another child.
        }
    }
}

static void housekasa_device_receive (int fd, int mode) {

    char data[1500];
    struct sockaddr_in addr;
    int addrlen = sizeof(addr);

    int size = recvfrom (KasaSocket, data, sizeof(data)-1, 0,
                         (struct sockaddr *)(&addr), &addrlen);
    if (size > 0) {
        int i;
        int key = 0xab;
        for (i = 0; i < size; ++i) {
            char tmp = data[i];
            data[i] = key ^ data[i];
            key = tmp;
        }
        data[size] = 0;
        if (echttp_isdebug()) fprintf (stderr, "Received: %s\n", data);

        ParserToken json[256];
        int jsoncount = 256;

        // We need to copy to preserve the original data (JSON decoding is
        // destructive).
        //
        char buffer[1500];
        strncpy (buffer, data, sizeof(buffer));

        const char *error = echttp_json_parse (buffer, json, &jsoncount);
        if (error) {
            houselog_trace (HOUSE_FAILURE, "DEVICE", "%s: %s", error, data);
            return;
        }

        int response = echttp_json_search (json, ".system.get_sysinfo");
        if (response >= 0) {
            housekasa_device_getinfo (json, jsoncount, &addr, data);
        } else {
            response = echttp_json_search (json, ".system.set_relay_state");
            if (response >= 0) {
                housekasa_device_response (json, jsoncount, &addr, data);
            }
        }
    }
}

const char *housekasa_device_initialize (int argc, const char **argv) {
    housekasa_device_socket ();
    echttp_listen (KasaSocket, 1, housekasa_device_receive, 0);
    return housekasa_device_refresh ();
}

