#include "MiraMqtt.h"
#include "MiraPanelUI.h"

MiraMqtt::MiraMqtt(MiraPanelServer& server, MiraPanelUI& ui)
  : _server(server), _ui(ui) {}

void MiraMqtt::begin() {
  _net.setTimeout(100);
  refreshConfig(true);
  Serial.println("[MIRA][MQTT] Initialisation");
  Serial.println("[MIRA][MQTT] Actions : " + actionPrefix() + "#topic#");
  Serial.println("[MIRA][MQTT] States  : " + statePrefix() + "#topic#");
  Serial.println("[MIRA][MQTT] Display : " + displayPrefix() + "<cle>/set|state");
}

String MiraMqtt::cleanScreenName(const String& input) {
  String out = input;
  out.trim();
  if (!out.length() || out == "null") out = "panel";
  while (out.startsWith("/")) out.remove(0, 1);
  while (out.endsWith("/")) out.remove(out.length() - 1);
  out.replace("#", "_");
  out.replace("+", "_");
  return out;
}

String MiraMqtt::actionPrefix() const {
  return "mira/" + _screenName + "/Actions/";
}

String MiraMqtt::statePrefix() const {
  return "mira/" + _screenName + "/States/";
}

String MiraMqtt::displayPrefix() const {
  return "mira/" + _screenName + "/Display/";
}

void MiraMqtt::refreshConfig(bool force) {
  if (!force && millis() - _lastConfigCheck < 2500UL) return;
  _lastConfigCheck = millis();

  bool enabled = _server.GetParamBool("MQTT_Actif");
  String host = _server.GetParamText("MQTT_Serveur");
  String portText = _server.GetParamText("MQTT_Port");
  String name = cleanScreenName(_server.GetParamText("MQTT_Nom"));
  String user = _server.GetParamText("MQTT_Utilisateur");
  String pass = _server.GetParamText("MQTT_Mot_de_passe");

  if (host == "null") host = "";
  if (portText == "null" || !portText.length()) portText = "1883";
  if (user == "null") user = "";
  if (pass == "null") pass = "";

  int parsedPort = portText.toInt();
  if (parsedPort <= 0 || parsedPort > 65535) parsedPort = 1883;

  String signature = String(enabled ? "1" : "0") + "|" + host + "|" + String(parsedPort) + "|" + name + "|" + user + "|" + pass;
  if (!force && signature == _lastConfigSignature) return;

  bool wasConnected = _connected;
  _enabled = enabled;
  _host = host;
  _port = (uint16_t)parsedPort;
  _screenName = name;
  _user = user;
  _pass = pass;
  _lastConfigSignature = signature;

  if (wasConnected) disconnectBroker();
  _lastConnectAttempt = 0;

  Serial.printf("[MIRA][MQTT] Config : actif=%s serveur=%s:%u nom=%s\n",
                _enabled ? "oui" : "non", _host.c_str(), _port, _screenName.c_str());
  Serial.println("[MIRA][MQTT] Actions : " + actionPrefix() + "#topic#");
  Serial.println("[MIRA][MQTT] States  : " + statePrefix() + "#topic#");
  Serial.println("[MIRA][MQTT] Display : " + displayPrefix() + "<cle>/set|state");
}

String MiraMqtt::makeClientId() const {
  uint64_t chip = ESP.getEfuseMac();
  char suffix[13];
  snprintf(suffix, sizeof(suffix), "%06llX", (unsigned long long)(chip & 0xFFFFFFULL));
  return String("mira-panel-") + suffix;
}

bool MiraMqtt::writeMqttString(uint8_t* buffer, size_t bufferSize, size_t& pos, const String& value) {
  size_t len = value.length();
  if (len > 65535 || pos + 2 + len > bufferSize) return false;
  buffer[pos++] = (uint8_t)((len >> 8) & 0xFF);
  buffer[pos++] = (uint8_t)(len & 0xFF);
  memcpy(buffer + pos, value.c_str(), len);
  pos += len;
  return true;
}

size_t MiraMqtt::encodeRemainingLength(uint32_t value, uint8_t* out, size_t outSize) {
  size_t n = 0;
  do {
    if (n >= outSize) return 0;
    uint8_t digit = value % 128;
    value /= 128;
    if (value > 0) digit |= 0x80;
    out[n++] = digit;
  } while (value > 0);
  return n;
}

bool MiraMqtt::sendPacket(uint8_t header, const uint8_t* variableAndPayload, size_t length) {
  if (!_net.connected()) return false;
  uint8_t rl[4];
  size_t rlLen = encodeRemainingLength(length, rl, sizeof(rl));
  if (!rlLen) return false;

  if (_net.write(&header, 1) != 1) return false;
  if (_net.write(rl, rlLen) != rlLen) return false;
  if (length && _net.write(variableAndPayload, length) != length) return false;
  _lastOutActivity = millis();
  return true;
}

bool MiraMqtt::readByte(uint8_t& value, uint32_t timeoutMs) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    int c = _net.read();
    if (c >= 0) {
      value = (uint8_t)c;
      return true;
    }
    delay(1);
  }
  return false;
}

bool MiraMqtt::readPacket(uint8_t& header, uint8_t* payload, size_t payloadSize, size_t& payloadLen, uint32_t timeoutMs) {
  payloadLen = 0;
  if (!readByte(header, timeoutMs)) return false;

  uint32_t remaining = 0;
  uint32_t multiplier = 1;
  uint8_t encoded = 0;
  uint8_t count = 0;
  do {
    if (!readByte(encoded, timeoutMs)) return false;
    remaining += (encoded & 127U) * multiplier;
    multiplier *= 128U;
    count++;
    if (count > 4) return false;
  } while ((encoded & 128U) != 0);

  if (remaining > payloadSize) {
    // Paquet trop grand : on le vide pour resynchroniser le flux.
    for (uint32_t i = 0; i < remaining; ++i) {
      uint8_t dummy;
      if (!readByte(dummy, timeoutMs)) break;
    }
    return false;
  }

  for (uint32_t i = 0; i < remaining; ++i) {
    if (!readByte(payload[i], timeoutMs)) return false;
  }
  payloadLen = (size_t)remaining;
  _lastInActivity = millis();
  return true;
}

bool MiraMqtt::connectBroker() {
  if (!_enabled || !_host.length() || WiFi.status() != WL_CONNECTED) return false;

  Serial.println("[MIRA][MQTT] Connexion au broker...");
  _net.stop();
  if (!_net.connect(_host.c_str(), _port, 180)) {
    Serial.println("[MIRA][MQTT] Echec de connexion au broker");
    _connected = false;
    return false;
  }

  uint8_t packet[MAX_PACKET];
  size_t pos = 0;

  // Variable header MQTT 3.1.1
  if (!writeMqttString(packet, sizeof(packet), pos, "MQTT")) return false;
  packet[pos++] = 0x04; // protocol level 4

  uint8_t flags = 0x02; // clean session
  bool useAuth = _user.length() || _pass.length();
  if (useAuth) flags |= 0x80; // username flag obligatoire si password flag est utilise
  if (_pass.length()) flags |= 0x40;
  packet[pos++] = flags;
  packet[pos++] = (uint8_t)((KEEPALIVE_S >> 8) & 0xFF);
  packet[pos++] = (uint8_t)(KEEPALIVE_S & 0xFF);

  String clientId = makeClientId();
  if (!writeMqttString(packet, sizeof(packet), pos, clientId)) return false;
  if (useAuth && !writeMqttString(packet, sizeof(packet), pos, _user)) return false;
  if (_pass.length() && !writeMqttString(packet, sizeof(packet), pos, _pass)) return false;

  if (!sendPacket(0x10, packet, pos)) {
    _net.stop();
    return false;
  }

  uint8_t header = 0;
  uint8_t response[16];
  size_t responseLen = 0;
  if (!readPacket(header, response, sizeof(response), responseLen, 250) || (header & 0xF0) != 0x20 || responseLen < 2 || response[1] != 0) {
    Serial.println("[MIRA][MQTT] Echec de connexion au broker");
    _net.stop();
    _connected = false;
    return false;
  }

  _connected = true;
  _pingOutstanding = false;
  _lastInActivity = millis();
  _lastOutActivity = millis();
  Serial.println("[MIRA][MQTT] Broker connecte");
  Serial.println("[MIRA][MQTT] Client ID : " + clientId);

  if (!subscribeStates()) {
    disconnectBroker();
    return false;
  }
  _lastDisplayStateSignature = "";
  publishDisplayStates(true);
  return true;
}

void MiraMqtt::disconnectBroker() {
  if (_net.connected()) {
    sendPacket(0xE0, nullptr, 0);
    _net.stop();
  }
  if (_connected) Serial.println("[MIRA][MQTT] Connexion au broker perdue");
  _connected = false;
  _pingOutstanding = false;
}

bool MiraMqtt::subscribeStates() {
  String stateTopic = statePrefix() + "#";
  String displayTopic = displayPrefix() + "+/set";
  uint8_t packet[MAX_PACKET];
  size_t pos = 0;
  uint16_t id = _messageId++;
  if (_messageId == 0) _messageId = 1;
  packet[pos++] = (uint8_t)((id >> 8) & 0xFF);
  packet[pos++] = (uint8_t)(id & 0xFF);
  if (!writeMqttString(packet, sizeof(packet), pos, stateTopic)) return false;
  if (pos >= sizeof(packet)) return false;
  packet[pos++] = 0;
  if (!writeMqttString(packet, sizeof(packet), pos, displayTopic)) return false;
  if (pos >= sizeof(packet)) return false;
  packet[pos++] = 0;
  bool ok = sendPacket(0x82, packet, pos);
  if (ok) {
    Serial.println("[MIRA][MQTT] Abonnement : " + stateTopic);
    Serial.println("[MIRA][MQTT] Commandes  : " + displayTopic);
  }
  return ok;
}

bool MiraMqtt::publishRaw(const String& topic, const String& payload, bool retained) {
  if (!_connected || !_net.connected()) return false;

  uint8_t packet[MAX_PACKET];
  size_t pos = 0;
  if (!writeMqttString(packet, sizeof(packet), pos, topic)) return false;
  if (pos + payload.length() > sizeof(packet)) return false;
  memcpy(packet + pos, payload.c_str(), payload.length());
  pos += payload.length();

  uint8_t header = retained ? 0x31 : 0x30; // PUBLISH QoS0
  return sendPacket(header, packet, pos);
}

bool MiraMqtt::publishAction(const String& topic, const String& payload) {
  if (!_enabled) return false;
  String full = actionPrefix() + topic;
  bool ok = publishRaw(full, payload, false);
  if (ok) Serial.printf("[MIRA][MQTT] Action %s = %s\n", full.c_str(), payload.c_str());
  return ok;
}

bool MiraMqtt::publishDisplayState(const String& key, const String& payload) {
  if (!_enabled) return false;
  return publishRaw(displayPrefix() + key + "/state", payload, true);
}

void MiraMqtt::publishDisplayStates(bool force) {
  if (!_connected || !_net.connected()) return;
  const uint32_t now = millis();
  if (!force && (uint32_t)(now - _lastDisplayStateCheckMs) < 15000UL) return;
  _lastDisplayStateCheckMs = now;
  String signature = String(_ui.displayDayBrightness()) + "|" + String(_ui.displayNightBrightness()) + "|" +
                     String(_ui.displaySleepBrightness()) + "|" + String(_ui.displaySleepTimeout()) + "|" +
                     (_ui.displayNightAuto() ? "1" : "0") + "|" + _ui.displayNightStart() + "|" +
                     _ui.displayNightEnd() + "|" + (_ui.displayClockInSleep() ? "1" : "0") + "|" +
                     String(_ui.displayActiveBrightness()) + "|" + (_ui.displaySleeping() ? "sleep" : "awake") + "|" +
                     (_ui.displayNightActive() ? "night" : "day");
  if (!force && signature == _lastDisplayStateSignature) return;
  _lastDisplayStateSignature = signature;

  publishDisplayState("day", String(_ui.displayDayBrightness()));
  publishDisplayState("night", String(_ui.displayNightBrightness()));
  publishDisplayState("sleep_brightness", String(_ui.displaySleepBrightness()));
  publishDisplayState("sleep_timeout", String(_ui.displaySleepTimeout()));
  publishDisplayState("night_auto", _ui.displayNightAuto() ? "1" : "0");
  publishDisplayState("night_start", _ui.displayNightStart());
  publishDisplayState("night_end", _ui.displayNightEnd());
  publishDisplayState("clock_sleep", _ui.displayClockInSleep() ? "1" : "0");
  publishDisplayState("brightness", String(_ui.displayActiveBrightness()));
  publishDisplayState("mode", _ui.displayNightActive() ? "night" : "day");
  publishDisplayState("state", _ui.displaySleeping() ? "sleep" : "awake");
}

bool MiraMqtt::sendPing() {
  if (!_connected) return false;
  bool ok = sendPacket(0xC0, nullptr, 0);
  if (ok) _pingOutstanding = true;
  return ok;
}

void MiraMqtt::processPublish(uint8_t header, const uint8_t* packet, size_t length) {
  if (length < 2) return;
  size_t pos = 0;
  uint16_t topicLen = ((uint16_t)packet[pos] << 8) | packet[pos + 1];
  pos += 2;
  if (pos + topicLen > length) return;

  String topic;
  topic.reserve(topicLen);
  for (uint16_t i = 0; i < topicLen; ++i) topic += (char)packet[pos + i];
  pos += topicLen;

  uint8_t qos = (header >> 1) & 0x03;
  if (qos > 0) {
    if (pos + 2 > length) return;
    pos += 2;
  }

  String payload;
  payload.reserve(length - pos);
  while (pos < length) payload += (char)packet[pos++];

  String states = statePrefix();
  if (topic.startsWith(states)) {
    String leaf = topic.substring(states.length());
    if (!leaf.length()) return;
    Serial.printf("[MIRA][MQTT] State %s => %s\n", topic.c_str(), payload.c_str());
    _ui.handleMqttState(leaf, payload);
    return;
  }

  String display = displayPrefix();
  if (topic.startsWith(display) && topic.endsWith("/set")) {
    String key = topic.substring(display.length(), topic.length() - 4);
    if (!key.length()) return;
    Serial.printf("[MIRA][MQTT] Display %s <= %s\n", key.c_str(), payload.c_str());
    _ui.handleMqttDisplayCommand(key, payload);
    publishDisplayStates(true);
  }
}

void MiraMqtt::processIncoming() {
  while (_net.connected() && _net.available()) {
    uint8_t header = 0;
    uint8_t packet[MAX_PACKET];
    size_t len = 0;
    if (!readPacket(header, packet, sizeof(packet), len, 120)) {
      break;
    }

    switch (header & 0xF0) {
      case 0x30: // PUBLISH
        processPublish(header, packet, len);
        break;
      case 0xD0: // PINGRESP
        _pingOutstanding = false;
        break;
      case 0x90: // SUBACK
        Serial.println("[MIRA][MQTT] Abonnement confirme");
        break;
      default:
        break;
    }
  }
}

void MiraMqtt::forceReconnect() {
  disconnectBroker();
  _lastConnectAttempt = 0;
}

void MiraMqtt::loop() {
  refreshConfig(false);

  if (!_enabled || !_host.length()) {
    if (_connected) disconnectBroker();
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (_connected) disconnectBroker();
    return;
  }

  if (!_net.connected()) {
    if (_connected) {
      _connected = false;
      Serial.println("[MIRA][MQTT] Connexion au broker perdue");
    }
    if (millis() - _lastConnectAttempt >= 10000UL || _lastConnectAttempt == 0) {
      _lastConnectAttempt = millis();
      connectBroker();
    }
    return;
  }

  _connected = true;
  processIncoming();
  publishDisplayStates(false);

  uint32_t now = millis();
  uint32_t idleIn = now - _lastInActivity;
  uint32_t idleOut = now - _lastOutActivity;

  if (_pingOutstanding && idleIn > (KEEPALIVE_S * 1500UL)) {
    disconnectBroker();
    return;
  }

  if (!_pingOutstanding && idleIn > KEEPALIVE_S * 1000UL && idleOut > KEEPALIVE_S * 1000UL) {
    sendPing();
  }
}
