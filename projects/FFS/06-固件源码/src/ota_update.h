#pragma once

class WebServer;
void otaSetup(WebServer& server);
void otaTick();
// Reuse the existing LAN, Host/Origin and per-boot nonce checks for device controls.
bool otaAuthorizeControl();
