#include <Arduino.h>
#include <lvgl.h>
#include "MiraPanelServer.h"
#include "MiraHardware.h"
#include "MiraPanelUI.h"
#include "MiraMqtt.h"

char ProgrammeNom[] = "Mira Panel";
MiraPanelServer Serveur(ProgrammeNom);
MiraDisplay Ecran;
MiraPanelUI Interface(Serveur, Ecran);
MiraMqtt Mqtt(Serveur, Interface);

// MQTT Mira actif. Il ne se connecte que si MQTT_Actif est active dans la page Web.
static constexpr bool MIRA_MQTT_RUNTIME_ENABLED = true;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serveur.Debug(true);
  Serveur.miraLvgl(true);
  Serveur.JeedomAutoRefresh(true);
  Serveur.Init();

  // Parametres MQTT proches du dernier firmware Mira.
  // Ils apparaissent dans la nouvelle page Web "MQTT" et restent persistants.
  Serveur.AddParamBool("MQTT_Actif", false);
  Serveur.AddParamText("MQTT_Nom", "panel");
  Serveur.AddParamText("MQTT_Serveur", "");
  Serveur.AddParamText("MQTT_Port", "1883");
  Serveur.AddParamText("MQTT_Utilisateur", "");
  Serveur.AddParamText("MQTT_Mot_de_passe", "", true);

  // Affichage autonome Mira Panel. Les valeurs sont aussi exposees en MQTT.
  Serveur.AddParamText("DISPLAY_Jour", "85");
  Serveur.AddParamText("DISPLAY_Nuit", "20");
  Serveur.AddParamText("DISPLAY_Veille", "8");
  Serveur.AddParamText("DISPLAY_Delai", "120");
  Serveur.AddParamBool("DISPLAY_NuitAuto", false);
  Serveur.AddParamText("DISPLAY_DebutNuit", "23:00");
  Serveur.AddParamText("DISPLAY_FinNuit", "07:00");
  Serveur.AddParamBool("DISPLAY_HorlogeVeille", true);

  Serveur.SetupModule(
    "Mira Panel",
    MIRA_HARDWARE_NAME,
    "0.7.7",
    "https://raw.githubusercontent.com/Arokh76/Mira-panel/main/firmware/",
    "LVGL"
  );

  Interface.begin();
  if (MIRA_MQTT_RUNTIME_ENABLED) {
    Interface.setMqtt(&Mqtt);
  }
  Interface.buildFromMiraJson();
  if (MIRA_MQTT_RUNTIME_ENABLED) {
    Mqtt.begin();
  } else {
    Serial.println("[MIRA][MQTT] Module desactive dans les parametres Web");
  }

  Serial.println();
  Serial.println("[MIRA] Firmware 0.7.7 initialise");
  Serial.printf("[MIRA][HW] Flash : %.1f MB\n", ESP.getFlashChipSize() / 1048576.0f);
  Serial.printf("[MIRA][HW] PSRAM : %.1f MB\n", ESP.getPsramSize() / 1048576.0f);
  Serial.println("[MIRA] Interface HTTP, LVGL et MQTT pretes");
  Serial.println("[MIRA][MQTT] States = affichage uniquement, aucune action Jeedom");
}

void loop() {
  static bool otaMaintenance = false;
  const bool otaRunning = Serveur.IsUpdateInProgress();

  if (otaRunning) {
    if (!otaMaintenance) {
      otaMaintenance = true;
      if (MIRA_MQTT_RUNTIME_ENABLED) Mqtt.forceReconnect();
      Serial.println("[MIRA][OTA] Mode maintenance actif : MQTT/UI suspendus");
    }
    delay(5);
    return;
  }

  if (otaMaintenance) {
    otaMaintenance = false;
    if (MIRA_MQTT_RUNTIME_ENABLED) Mqtt.forceReconnect();
    Serial.println("[MIRA][OTA] Mode maintenance termine");
  }

  // Evenements commandes : /updatenr et interactions de l'ecran.
  int miraEvent = Serveur.IndexGetEvent();
  if (miraEvent >= 0) Interface.handleMiraEvent(miraEvent);

  // Evenements de programmation Mira : remove/add/save de la liste LVGL.
  int paramEvent = Serveur.ParamGetEvent();
  if (paramEvent >= 0) Interface.handleParamEvent(paramEvent);

  Interface.loop();
  if (MIRA_MQTT_RUNTIME_ENABLED) Mqtt.loop();
  delay(2);
}
