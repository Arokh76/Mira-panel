#ifndef MiraPanelServer_h
#define MiraPanelServer_h
#include "Arduino.h"
#include <WiFi.h>
#include "src/ESPAsyncWebServer.h"
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include "Cipher.h"


#define ACTION_UPDATE -2


#define MIRA_FORMAT 0
#define MIRA_ADD_LIST_LVGL 1
#define MIRA_SAVE_LIST_LVGL 2
#define MIRA_REMOVE_LIST_LVGL 3



class MiraPanelServer {

public:
  MiraPanelServer(char* NomProgramme);

  //Nouveau
  //obsolete: resetevent
  //Diminution de la taille des variables et suppression des variables inutiles

  void ResetButton(int PinButton = 0, int PinLed = 2);  //10 secondes: la led s'allume, 15 secondes, Reset total des SPIFFS
  void RSTHandler();
  void IndexSetEvent(int ID);
  void AsyncHTTP(String URL);
  void JeedomInit();  // Copie les commandes jeedom dans le JSON index
  void JeedomCmd(int ID);
  void GenericCmd(int ID);

  void miraLvgl(bool mira_lvgl = true);
  String GetJsonLvgl();
  String GetCameraUrl(const String& name); // Jeedom Commandes: persistent camera URL
  int ParamGetEvent();
  // Reconstruction WT32: remet a zero uniquement la table des actions dynamiques
  // afin de reconstruire l'interface LVGL sans redemarrer le module.
  void ResetActionsForLvgl();
  bool WT32HasAction(const String& name);
  bool WT32ExecuteAction(const String& name);
  bool WT32HasToggle(const String& name);
  bool WT32ExecuteToggle(const String& name, bool on);
  bool WT32ReadToggle(const String& name, bool& state);
  // Debounced, read-only Jeedom notification after local GPIO transitions.
  void WT32NotifyGpioChanges();
  bool WT32MultiVisible(const String& name);
  void WT32PollMultiButtons();

  int IndexSetListByOption(String NameList, String Option);


  //Setup
  void SetUser(char* ID_USER);
  void Init();

  void AddParamText(String Name, String Default, bool Hidden = false);
  void AddParamBool(String Name, bool Default);
  void AddListParam(String Name, String Default, bool Hidden = false, bool Boolean = false);
  void CreateListParam();
  void UpdateListParam(String Name, String Value);
  void SaveListParam();

  int IndexAddButton(String Name, bool Return_Line = true);
  int IndexAddLittleButton(String Name, bool Return_Line = true);
  void IndexAddTitle(String Name, String Size = "80");
  int IndexAddInfo(String Name, String Unit = "", String Size = "60");
  int* IndexAddToogle(String Name, String Value_0 = "Off", String Value_1 = "On", String Default = "Off");
  int* IndexAddToggle(String Name, String Value_0 = "Off", String Value_1 = "On", String Default = "Off");
  int IndexAddColor(String Name, int Default_R = 50, int Default_G = 120, int Default_B = 180);
  int IndexAddRange(String Name, int Min = 0, int Max = 100, int Default = 0);
  int IndexAddList(String NameList, String Name, String Option = "");
  int IndexAddInputBox(String Name, String Type);
  int IndexAddTooltip(String Name, String Content, bool Return_Line = true);
  int AddHTTPAction(String Name);

  void IndexDarkColor(String HTML_Color);
  void IndexLightColor(String HTML_Color);
  void IndexActiveColor(String HTML_Color);
  void IndexTextColor(String HTML_Color);

  void IndexAddHTML(String Value);
  void IndexBackgroundStart();
  void IndexBackgroundFinish();
  void IndexTileStart(String Width = "700px", String Height = "", String HTML_Color = "");
  void IndexTileWhiteStart(String Width, String Height, String HTML_Border_Color);
  void IndexTileFinish();
  void IndexContainerStart(int Width);
  void IndexContainerFinish();
  void IndexAddSeg();
  void IndexAddSegWhite();
  void IndexAddText(String Name, String Size = "25");

  void IndexTableStart();
  void IndexTableFinish();
  void IndexRowStart();
  void IndexRowFinish();
  void IndexCellStart(int Colspan, int Rowspan, String Align, String Width, String Height, bool border);
  void IndexCellFinish();

  void SetupModule(String Module, String ID_Module = "", String Version = "", String MAJ = "", String Type = "");
  void Jeedom(bool Value = true);
  void JeedomAutoRefresh(bool Value = false);
  void GenericAutoRefresh(bool Value = false);

  //Fonctions cachée !! ah ah ah!
  void IndexBottomText(String Text);
  void IndexAddLogo(String URL, int Width, int Height);
  void IndexAddLittleLogo(String URL, int Width, int Height);

  //Loop
  int IndexGetEvent();
  //Ajouter HANDLE

  //Autre
  String GetParamText(String Name);
  bool GetParamBool(String Name);
  void RemoveParam(String Name);
  void UpdateParamText(String Name, String Value);
  void UpdateParamBool(String Name, bool Value);

  bool ResetParams();
  bool ResetNetwork();
  void ResetAll();

  String IndexGetInfo(int ID);
  String IndexGetToogle(int ID);
  String IndexGetToggle(int ID);
  float IndexGetRange(int ID);
  int IndexGetColor_R(int ID);
  int IndexGetColor_G(int ID);
  int IndexGetColor_B(int ID);
  String IndexGetInputBox(int ID);

  void IndexSetInfo(int ID, String Value);
  void IndexSetRange(int ID, float Value);
  void IndexSetToogle(int ID);
  void IndexSetToggle(int ID);
  void IndexSetInputBox(int ID, String Value);
  void IndexSetColor(int ID, int R, int G, int B);
  void IndexSetList(int ID);

  void IndexResetEvent();

  String GetKey();
  String GetIP();

  void JeedomRefresh();

  String HTTPGet(String URL);
  void HTTPPost(String URL);

  int GetWiFiStatus();
  bool IsSetupMode();
  bool IsUpdateInProgress();

  void Debug(bool Value = true);
  void Scan_Network(bool Value = false);


private:
  bool initPA();
  bool initWiFi();
  bool ReInitWiFi();
  bool WiFiConnect();
  void JeedomTimerRefresh();

  void ServeurDNS();
  void ServeurStation();


  static String RetourVariable(String var);
  void ScanWiFi();
  String ScanWiFiReco();


  //void EventInclusions();

  bool update_module(String fileName);
  //void handleDoUpdate(AsyncWebServerRequest *request, const String &filename, size_t index, uint8_t *data, size_t len, bool final);

  void SendQueud();
  void ReorderQueued();

  struct Queued {
    int id = -1;
    String cmd;
    String value;
    bool send;
    byte test;
  };

  Queued eQueud[5];
};





#endif