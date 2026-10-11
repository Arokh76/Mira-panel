#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "MiraPanelServer.h"

class MiraPanelUI;

// Client MQTT minimaliste MQTT 3.1.1 / QoS 0.
// Il est volontairement embarque dans le sketch afin de ne pas ajouter
// une bibliotheque Arduino supplementaire a installer.
class MiraMqtt {
public:
  MiraMqtt(MiraPanelServer& server, MiraPanelUI& ui);

  void begin();
  void loop();
  void forceReconnect();

  bool enabled() const { return _enabled; }
  bool connected() const { return _connected; }
  String screenName() const { return _screenName; }
  String actionPrefix() const;
  String statePrefix() const;
  String displayPrefix() const;
  bool publishDisplayState(const String& key, const String& payload);

  // Publication d'une action issue de l'ecran.
  // Aucun etat n'est publie automatiquement au demarrage : cela evite
  // qu'un reboot puisse declencher une commande domotique.
  bool publishAction(const String& topic, const String& payload);

private:
  MiraPanelServer& _server;
  MiraPanelUI& _ui;
  WiFiClient _net;

  bool _enabled = false;
  bool _connected = false;
  bool _pingOutstanding = false;

  String _host;
  uint16_t _port = 1883;
  String _screenName = "panel";
  String _user;
  String _pass;

  String _lastConfigSignature;
  uint32_t _lastConfigCheck = 0;
  uint32_t _lastConnectAttempt = 0;
  uint32_t _lastInActivity = 0;
  uint32_t _lastOutActivity = 0;
  uint16_t _messageId = 1;

  static constexpr uint16_t KEEPALIVE_S = 20;
  static constexpr size_t MAX_PACKET = 768;

  void refreshConfig(bool force = false);
  bool connectBroker();
  void disconnectBroker();
  bool subscribeStates();
  bool publishRaw(const String& topic, const String& payload, bool retained = false);
  void publishDisplayStates(bool force = false);
  String _lastDisplayStateSignature;
  uint32_t _lastDisplayStateCheckMs = 0;
  bool sendPing();

  bool sendPacket(uint8_t header, const uint8_t* variableAndPayload, size_t length);
  size_t encodeRemainingLength(uint32_t value, uint8_t* out, size_t outSize);
  bool writeMqttString(uint8_t* buffer, size_t bufferSize, size_t& pos, const String& value);

  bool readPacket(uint8_t& header, uint8_t* payload, size_t payloadSize, size_t& payloadLen, uint32_t timeoutMs = 80);
  bool readByte(uint8_t& value, uint32_t timeoutMs);
  void processIncoming();
  void processPublish(uint8_t header, const uint8_t* packet, size_t length);

  String makeClientId() const;
  static String cleanScreenName(const String& input);
};
