/* HouseKasa - A simple home web server for control of TP-Link Kasa devices.
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
 * housekasa.c - Main loop of the housekasa program.
 *
 * SYNOPSYS:
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "echttp.h"
#include "echttp_cors.h"
#include "echttp_json.h"
#include "echttp_static.h"
#include "houseportalclient.h"
#include "housediscover.h"
#include "houselog.h"
#include "houseconfig.h"
#include "housedepositor.h"

#include "housekasa_device.h"

static const char *housekasa_status (const char *method, const char *uri,
                                    const char *data, int length) {
    static char buffer[65537];
    ParserToken token[1024];
    char pool[65537];
    char host[256];
    int count = housekasa_device_count();
    int i;

    gethostname (host, sizeof(host));

    ParserContext context = echttp_json_start (token, 1024, pool, 65537);

    int root = echttp_json_add_object (context, 0, 0);
    echttp_json_add_string (context, root, "host", host);
    echttp_json_add_string (context, root, "proxy", houseportal_server());
    echttp_json_add_integer (context, root, "timestamp", (long)time(0));
    int top = echttp_json_add_object (context, root, "control");
    int container = echttp_json_add_object (context, top, "status");

    for (i = 0; i < count; ++i) {
        time_t pulsed = housekasa_device_deadline(i);
        const char *name = housekasa_device_name(i);
        const char *status = housekasa_device_failure(i);
        int priority = housekasa_device_priority(i);
        if (!status) status = housekasa_device_get(i)?"on":"off";
        const char *commanded = housekasa_device_commanded(i)?"on":"off";

        int point = echttp_json_add_object (context, container, name);
        echttp_json_add_string (context, point, "state", status);
        echttp_json_add_string (context, point, "command", commanded);
        if (pulsed)
            echttp_json_add_integer (context, point, "pulse", (int)pulsed);
        if (priority)
            echttp_json_add_bool (context, point, "priority", priority);
        echttp_json_add_string (context, point, "gear", "light");
    }
    const char *error = echttp_json_export (context, buffer, 65537);
    if (error) {
        echttp_error (500, error);
        return "";
    }
    echttp_content_type_json ();
    return buffer;
}

static const char *housekasa_set (const char *method, const char *uri,
                                 const char *data, int length) {

    const char *point = echttp_parameter_get("point");
    const char *statep = echttp_parameter_get("state");
    const char *pulsep = echttp_parameter_get("pulse");
    const char *cause = echttp_parameter_get("cause");
    int state;
    int pulse;
    int i;
    int count = housekasa_device_count();
    int found = 0;

    if (!point) {
        echttp_error (404, "missing point name");
        return "";
    }
    if (!statep) {
        echttp_error (400, "missing state value");
        return "";
    }
    if ((strcmp(statep, "on") == 0) || (strcmp(statep, "1") == 0)) {
        state = 1;
    } else if ((strcmp(statep, "off") == 0) || (strcmp(statep, "0") == 0)) {
        state = 0;
    } else {
        echttp_error (400, "invalid state value");
        return "";
    }

    pulse = pulsep ? atoi(pulsep) : 0;
    if (pulse < 0) {
        echttp_error (400, "invalid pulse value");
        return "";
    }

    for (i = 0; i < count; ++i) {
       if ((strcmp (point, "all") == 0) ||
           (strcmp (point, housekasa_device_name(i)) == 0)) {
           found = 1;
           housekasa_device_set (i, state, pulse, cause);
       }
    }

    if (! found) {
        echttp_error (404, "invalid point name");
        return "";
    }
    return housekasa_status (method, uri, data, length);
}

static const char *housekasa_config (const char *method, const char *uri,
                                  const char *data, int length) {

    if (strcmp ("GET", method) == 0) {
        static char buffer[65537];
        housekasa_device_live_config (buffer, sizeof(buffer));
        echttp_content_type_json ();
        return buffer;
    } else if (strcmp ("POST", method) == 0) {
        const char *error = houseconfig_update(data);
        if (error) {
            echttp_error (400, error);
        } else {
            housekasa_device_refresh();
            houselog_event ("SYSTEM", "CONFIG", "SAVE", "TO DEPOT %s", houseconfig_name());
            housedepositor_put ("config", houseconfig_name(), data, length);
        }
    } else {
        echttp_error (400, "invalid method");
    }
    return "";
}

static void housekasa_background (int fd, int mode) {

    time_t now = time(0);

    houseportal_background (now);
    housekasa_device_periodic(now);
    if (housekasa_device_changed()) {
        static char buffer[65537];
        housekasa_device_live_config (buffer, sizeof(buffer));
        houseconfig_update(buffer);
        houselog_event ("SYSTEM", "CONFIG", "SAVE", "TO DEPOT %s (AUTODETECT)", houseconfig_name());
        housedepositor_put ("config", houseconfig_name(), buffer, strlen(buffer));
        if (echttp_isdebug()) fprintf (stderr, "Configuration saved\n");
    }
    housediscover (now);
    houselog_background (now);
    housedepositor_periodic (now);
}

static void housekasa_config_listener (const char *name, time_t timestamp,
                                       const char *data, int length) {
    houselog_event ("SYSTEM", "CONFIG", "LOAD", "FROM DEPOT %s", name);
    if (!houseconfig_update (data)) housekasa_device_refresh();
}

static void housekasa_protect (const char *method, const char *uri) {
    echttp_cors_protect(method, uri);
}

int main (int argc, const char **argv) {

    const char *error;

    // These strange statements are to make sure that fds 0 to 2 are
    // reserved, since this application might output some errors.
    // 3 descriptors are wasted if 0, 1 and 2 are already open. No big deal.
    //
    open ("/dev/null", O_RDONLY);
    dup(open ("/dev/null", O_WRONLY));

    signal(SIGPIPE, SIG_IGN);

    echttp_default ("-http-service=dynamic");

    argc = echttp_open (argc, argv);
    if (echttp_dynamic_port()) {
        static const char *path[] = {"control:/kasa"};
        houseportal_initialize (argc, argv);
        houseportal_declare (echttp_port(4), path, 1);
    }
    housediscover_initialize (argc, argv);
    houselog_initialize ("kasa", argc, argv);
    housedepositor_initialize (argc, argv);

    houseconfig_default ("--config=kasa");
    error = houseconfig_load (argc, argv);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, "CONFIG", "Cannot load configuration: %s\n", error);
    }
    error = housekasa_device_initialize (argc, argv);
    if (error) {
        houselog_trace
            (HOUSE_FAILURE, "PLUG", "Cannot initialize: %s\n", error);
        exit(1);
    }
    housedepositor_subscribe ("config", houseconfig_name(), housekasa_config_listener);

    echttp_cors_allow_method("GET");
    echttp_protect (0, housekasa_protect);

    echttp_route_uri ("/kasa/status", housekasa_status);
    echttp_route_uri ("/kasa/set",    housekasa_set);

    echttp_route_uri ("/kasa/config", housekasa_config);

    echttp_static_route ("/", "/usr/local/share/house/public");
    echttp_background (&housekasa_background);
    houselog_event ("SERVICE", "kasa", "STARTED", "ON %s", houselog_host());
    echttp_loop();
}

