# HouseKasa's Gallery

This gallery is meant to give an idea of the HouseKasa web UI. Note that HouseKasa is mainly a behind-the-scene service, a protocol converter for the TP-Link Kasa devices, and thus its UI is minimal.

The HouseKasa main web page acts as a sort of dashboard for the manual control of devices:

![HouseKasa Main Page](https://raw.githubusercontent.com/pascal-fb-martin/housekasa/main/gallery/main-page.png)

The top line is a navigation bar that gives access to the HouseKasa major pages.

The table lists all the known Kasa devices, and their status. The control button can be used to turn each device on or off manuall. This is mostly meant for testing--see [HouseLights](https://github.com/pascal-fb-martin/houselights) for a whole-house light control service.

The event page shows a record of the major changes detected by the HouseKasa service. It's main purpose is to help troubleshoot issues with device discovery and control.

![HouseKasa Event Page](https://raw.githubusercontent.com/pascal-fb-martin/housekasa/main/gallery/event-page.png)

The configuration page is used to name discovered devices, or manually add devices. Note that HouseKasa automatically discover new devices when they become active, so the ID and CHILD elements typically never need to be filled.

![HouseKasa Config Page](https://raw.githubusercontent.com/pascal-fb-martin/housekasa/main/gallery/config-page.png)

