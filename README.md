# HouseKasa
A House web service to control TP-Link Kasa devices (lights, plugs..)

## Overview

This is a web server to give access to TP-Link Kasa WiFi devices. This server can sense the current status and control the state of each device. The web API is meant to be compatible with the House control API (e.g. the same web API as [houserelays](https://github.com/pascal-fb-martin/houserelays)).

See the [gallery](https://github.com/pascal-fb-martin/housekasa/blob/main/gallery/README.md) for a view of HouseKasa's web UI.

This service is not really meant to be accessed directly by end-users: these should use [houselights](https://github.com/pascal-fb-martin/houselights) to control Kasa devices.

So far HouseKasa has been tested with the following US models:
* HS220
* HS200
* KP400
* EP10

## Installation

* Install the OpenSSL development package(s).
* Install [echttp](https://github.com/pascal-fb-martin/echttp).
* Install [houseportal](https://github.com/pascal-fb-martin/houseportal).
* Clone this GitHub repository.
* make
* sudo make install
* Edit /etc/house/kasa.json (see below)

Otherwise installing [houselights](https://github.com/pascal-fb-martin/houselights) is recommended, but not necessarily on the same computer.

## Configuration
The preferred method is to configure the devices from the Configure web page.
The configuration is stored in file /etc/house/kasa.json. A typical example of configuration is:
```
{
    "kasa" : {
        "devices" : [
            {
                "name" : "light1",
                "address" : "xxxxxxxxxx",
                "description" : "a Kasa light"
            },
            {
                "name" : "light2",
                "address" : "xxxxxxxxxx",
                "description" : "another Kasa light"
            }
        ]
    }
}
```
## Device Setup
Each device must be setup using the Kasa phone app. The protocol for setting up devices has not been reverse engineered at that time.

## Kasa Protocol

This section describes the subset of the Kasa protocol that is implemented in HouseKasa. This is not a complete description of the protocol, only of the subset implemented by HouseKasa: others have provided more extensive documentation of that protocol (such as [softCheck](https://github.com/softScheck/tplink-smartplug/blob/master/tplink-smarthome-commands.txt)).

HouseKasa actually uses a variant of the Kasa protocol not widely described on the Internet, and that does not exactly match what the Kasa app is using. In this section we will first describe how the Kasa app interacts with some Kasa device types, and then describe what HouseKasa actually uses.

Note that some discrepancies in the protocol used by different device types might be attributed to the firmware version: when identifying a device it is recommended to consider both the type of device and the firmware version.

All commands and responses contain an encrypted JSON structure. The JSON structures are described based on examples of real network captures, with some values obscured.

The encryption cypher is a byte XOR operation with the previous (encrypted) bytes, except for the first byte which is encrypted with 0xab. Since XOR is its own inverse, encryption and decryption are the same operation, except for which byte value to use as the key for the next byte: either the original value (decryption) or the values after the XOR (encryption).

The Kasa protocol uses port 9999, both for UDP and TCP.

UDP packets contain the encrypted JSON as-is. The size of the JSON data is determined the size of the UDP packet.

TCP-based commands are prefixed with a big-endian 4 bytes integer, which represents the length of the JSON string that follows (the TCP protocol provide no information about the structure of the application data, so applications typically must use their own "framing" method).

#### HS220

The example bellow shows the discovery of a HS220 (US model), using UDP:
```
> {"system":{"get_sysinfo":{}}}
< {"system":{"get_sysinfo":{"sw_ver":"1.5.11 Build 200214 Rel.152651",
  "hw_ver":"1.0","mic_type":"IOT.SMARTPLUGSWITCH","model":"HS220(US)",
  "mac":"xx:xx:xx:xx:xx:xx","dev_name":"Smart Wi-Fi Dimmer",
  "alias":"TP-LINK_Smart Dimmer_ECC6","relay_state":0,"brightness":87,
  "on_time":0,"active_mode":"none","feature":"TIM","updating":0,"icon_hash":"",
  "rssi":-57,"led_off":0,"longitude_i":0,"latitude_i":0,
  "hwId":"xxxxxxxxxxxxxxxxxxxxxxxxxxxx95BD",
  "fwId":"00000000000000000000000000000000",
  "deviceId":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx6D52",
  "oemId":"xxxxxxxxxxxxxxxxxxxxxxxxxxxx4CD6",
  "preferred_state":[{"index":0,"brightness":100},{"index":1,"brightness":75},{"index":2,"brightness":50},{"index":3,"brightness":25}],
  "next_action":{"type":-1},"err_code":0}}}
```
In the example above, the system.get_sysinfo command returns a long structure that indicates the model of the device (system.get_sysinfo.model) and its device ID (system.get_sysinfo.deviceID.

This device is a single dimmer, and the state is directly part of the system.get_sysinfo structure (e.g. system.get_sysinfo.relay_state). It has four preferred levels (in array system.get_sysinfo.preferred_state: 100%, 75%, 50% and 25%). These preferred levels are shown on the phone app as shortcuts in the dimmer control screen.

The device can be turned on and of, without changing the dimmer level:
```
> {"context":{"source":"xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"},"smartlife.iot.dimmer":{"set_switch_state":{"state":0}}}
< {"smartlife.iot.dimmer":{"set_switch_state":{"err_code":0}}}
```
The "smartlife.iot.dimmer.set_switch_state.state" item controls if the light is on (1) or off (0).

The context structure is optional. 

The brightness level is also controllable:
```
> {"context":{"source":"xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"},
  "smartlife.iot.dimmer":{"set_brightness":{"brightness":75},
  "set_switch_state":{"state":1}}}
< {"smartlife.iot.dimmer":{"set_brightness":{"err_code":0},
  "set_switch_state":{"err_code":0}}}
```
That command actually achieves two results: it both sets the dimmer level ("smartlife.iot.dimmer.set_brightness.brightness") and turns the light on ("smartlife.iot.dimmer.set_switch_state.state"). I guess the brightness level has little immediate effect when the light is turned off anyway.

(HouseKasa does not support controlling the dimmer level at this time.)

#### KP400

The example below shows the discovery of a KP400 (US model), using UDP:
```
> {"system":{"get_sysinfo":{}}}
< {"system":{"get_sysinfo":{"sw_ver":"1.0.6 Build 200821 Rel.090909",
  "hw_ver":"2.0","model":"KP400(US)",
  "deviceId":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx042E",
  "oemId":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxE646",
  "hwId":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxBE38","rssi":-52,"longitude_i":0,
  "latitude_i":0,"alias":"TP-LINK_Smart Plug_BC6F","status":"new",
  "mic_type":"IOT.SMARTPLUGSWITCH","feature":"TIM","mac":"xx:xx:xx:xx:xx:xx",
  "updating":0,"led_off":0,
  "children":[{"id":"00","state":1,"alias":"Kasa_Smart Plug_BC6F_0","on_time":20,"next_action":{"type":-1}},{"id":"01","state":1,"alias":"Kasa_Smart Plug_BC6F_1","on_time":20,"next_action":{"type":-1}}],
  "child_num":2,"ntc_state":0,"err_code":0}}}
```
The JSON returned is similar, but not all the same items are present (probably due to the different firmware version).

This device actually control two outlets independently, which is described in array "system.get_sysinfo.children". Note that each outlet has a two-character ID (00 and 01).

Note the "system.get_sysinfo.deviceID" value: this will be important later, when controlling each outlet.

Each outlet can be controlled independently by providing an identifier for that outlet. The outlet ID is the concatenation of the deviceID and outlet ID:
```
> {"context":{"child_ids":["xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx042E00"],
  "source":"xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"},
  "system":{"set_relay_state":{"state":1}}}
< {"system":{"set_relay_state":{"err_code":0}}}
```
The array context.child_ids indicates which outlet(s) should be controlled. Apparently the protocol allows controlling multiple outlets in one command. The "system.set_relay_state.state" value indicates if the outlet should be turned off (0) or on (1).

The source item is optional. The context struct is mandatory (at least if one wants to control a subset of the outlets).

#### Alias

Each device or outlet can be assigned an alias, which is a way for the user to give a name to each:
```
> {"system":{"set_dev_alias":{"alias":"xxxxxxx"}}}
< {"system":{"set_dev_alias":{"err_code":0}}}
```

Note that a multi-plug device like the KP400 has both a global alias and an individual alias for each plug.

### How HouseKasa Uses the Kasa Protocol

This section describes what subset and variant of the Kasa protocol is used by the HouseKasa service.

The Kasa devices actually respond to all commands on the UDP port, not just to discovery commands. Thus HouseKasa only use UDP, port 9999.

Discovery of which devices are present on the network is made on a periodic basis (about every 30 seconds) using a UDP broadcast with the command:

```
{"system":{"get_sysinfo":{}}}
```
HouseKasa searches for the following items in the response:
```
system.get_sysinfo.model
system.get_sysinfo.alias (if not set in HouseKasa)
system.get_sysinfo.relay_state (may not be present)
system.get_sysinfo.deviceId
system.get_sysinfo.children[].id
system.get_sysinfo.children[].state (may not be present)
system.get_sysinfo.children[].alias (if not set in HouseKasa)
```

If the array "system.get_sysinfo.children" is present, its state and alias elements take precedence over "system.get_sysinfo.relay_state" and "system.get_sysinfo.alias".

The state of every known device is maintained by doing an unicast system.get_sysinfo request every 10 seconds, except after issuing a command, in which case the request is made at higher frequency for a minute, or else until the requested state was reached.

The Kasa devices (KP400 and HS220) all accept the "system.set_relay_state" command. However since the KP400 has two outlets, the exact outlet targetted must be specified using "context.child_ids". It happens that the HS220 also accept the presence of an outlet ID: it just ignores it. So the command sent for on and off is:
```
{"context":{"child_ids":["xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"]},"system":{"set_relay_state":{"state":x}}}
```

HouseKasa uses the response only as a prompt for sending a state request ("system.get_sysinfo"). This is because the response does not provide any context, or the state of the device. For example, a response from a KP400 does not indicate which plug this is related to. Since the only reliable information is the device IP address (UDP packet source adress), the simplest is to immediately query that device.

HouseKasa will query the state of each known device periodically (unicast UDP packet) to verify that the device is still present and to maintain its state current (the device could be controlled by others).

