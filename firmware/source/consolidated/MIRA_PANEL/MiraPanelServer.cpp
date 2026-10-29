/*#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Cipher.h>*/

#include <Update.h>
#include <esp_heap_caps.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>  // persisted Jeedom-controlled navigation


#include "MiraPanelServer.h"
#include "SPIFFS_Process.h"
#include "Async_HTTP.h"
#include "doUpdate.h"

#include "HTML/HTML_ManageurWiFi.h"
#include "HTML/HTML_ManageurWiFiSansR.h"
#include "HTML/HTML_Parametres.h"
#include "HTML/HTML_MQTT.h"
#include "HTML/HTML_Index.h"
#include "HTML/HTML_Sys.h"
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
#include "HTML/HTML_GPIO.h"
#include "MiraWT32Gpio.h"
#endif
#include "HTML/HTML_ReseauxEnreg.h"
#include "HTML/HTML_Message.h"
#include "HTML/HTML_ConfigAP.h"
#include "HTML/HTML_Donnees.h"
#include "HTML/HTML_ModificationReseau.h"
#include "HTML/HTML_Key.h"
#include "HTML/HTML_Enregistrement.h"
#include "MiraDisplayProfile.h"

char* NomProg = "Hello!";
char* Ident_ID = "admin";

String StringAdresseIP;
int TentativesReco = 0;
bool ConnexionPerdue = false;

//Async
AsyncWebSocketClient* globalClient = NULL;
IPAddress localIP;
// DHCP and manual addresses are now stored per Wi-Fi network.
static bool miraReadNetworkForm(AsyncWebServerRequest* req, String& err);
AsyncWebServer server(80);
AsyncEventSource events("/events");
String ScanReseaux[10];

long MomentPrecedent_Surv = 0;
String TITRE = "";
String MESSAGE = "";
String IDNetwork = "";
int IDAction = 1;
bool DNS = false;
DNSServer dnsServer;

//Json Action
int Action = -1;

bool SetupModuleDone = false;
String MiraRuntimeVersion = "";
String UpdateURL = "";

static const char* MIRA_GITHUB_VERSION_URL = "https://raw.githubusercontent.com/Arokh76/Mira-panel/firmware-beta/firmware/targets.json";
static const char* MIRA_GITHUB_BIN_URL = "https://raw.githubusercontent.com/Arokh76/Mira-panel/main/firmware/Mira_Panel.bin";

static int miraCompareVersion(const String& a, const String& b) {
  int ai = 0, bi = 0;
  for (int part = 0; part < 4; ++part) {
    long av = 0, bv = 0;
    while (ai < (int)a.length() && !isDigit(a[ai])) ai++;
    while (bi < (int)b.length() && !isDigit(b[bi])) bi++;
    while (ai < (int)a.length() && isDigit(a[ai])) { av = av * 10 + (a[ai++] - '0'); }
    while (bi < (int)b.length() && isDigit(b[bi])) { bv = bv * 10 + (b[bi++] - '0'); }
    if (av < bv) return -1;
    if (av > bv) return 1;
  }
  return 0;
}


static String miraCameraNvsKey(const String& name) {
  // Namespace keys are at most 15 chars. Stable 32-bit FNV-1a of the
  // technical widget name, never derived from Jeedom's volatile command ID.
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < name.length(); ++i) {
    hash ^= static_cast<uint8_t>(name[i]);
    hash *= 16777619UL;
  }
  char key[12];
  snprintf(key, sizeof(key), "c%08lx", static_cast<unsigned long>(hash));
  return String(key);
}

static bool miraCameraSafeUrl(const String& url) {
  return (url.isEmpty() ||
          (url.startsWith("http://") && url.length() <= 768 &&
           url.indexOf('\n') < 0 && url.indexOf('\r') < 0 &&
           url.indexOf(' ') < 0 && url.indexOf('@') < 0));
}

String ScriptConstruct = "";
bool ModeAP = false;
unsigned long previousMillis = 0;
const long timeout = 10000;

//Pour affichage des listes
String Memoire_Liste[99];
int ID_Memoire_Liste = 0;

//Mise en mémoire du statut de la liste
String Listes[99];
String Listes_Value[99];
int NB_Listes = 0;


String DarkColor = "#1E5064";
String LightColor = "#7193A0";
String ActiveColor = "#D39E55";
String TextColor = "#FFFFFF";

String Powered = "";

String Logo_URL = "";
int Logo_Width = 0;
int Logo_Height = 0;

String Little_Logo_URL = "";
int Little_Logo_Width = 0;
int Little_Logo_Height = 0;
bool BesoinCo = false;

bool JEEDOM_mira = false;
bool AUTO_REFRESH = true;
bool ActionGet = false;

bool RST_Button = false;
int Bouton_RST = 0;
int Led_RST = 0;
bool DoOneRST = false;
unsigned long RST_Moment_Present = 0;
unsigned long RST_Moment_Memorise = 0;



//unsigned long JEEDOM_Moment_Present = 0;
//unsigned long JEEDOM_Moment_Memorise = 0;

unsigned long Queued_Moment_Memorise = 0;

class GestionnairePortailCaptif : public AsyncWebHandler {
public:
  GestionnairePortailCaptif() {}
  virtual ~GestionnairePortailCaptif() {}
  bool canHandle(AsyncWebServerRequest* request) {
    request->addInterestingHeader("ANY");
    return true;
  }
  void handleRequest(AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", Page_Key, RetourVariable);
  }
  static String RetourVariable(String var) {
    String RETOUR = "Null";
    if (var == "SCANRESEAUX") {
      RETOUR = "<label for=\"RS\">Réseaux détectés</label><select id=\"RS\" name=\"RS\" class=\"box\" onchange=\"Click(this.value)\">";
      for (int j = 0; j < 10; j++) {
        if (ScanReseaux[j] != "") {
          RETOUR += "<option value=\"" + ScanReseaux[j] + "\">" + ScanReseaux[j] + "</option>";
        }
      }
      RETOUR += "</select>";
    }


    if (var == "APP") RETOUR = NomProg;
    if (var == "KEY") RETOUR = Uncrypt_Pass;
    if (var == "SSID") {
      if (SPIFFS_SSID == "null") {
        RETOUR = "";
      } else {
        RETOUR = SPIFFS_SSID;
      }
    }

    if (var == "IP") {
      if (SPIFFS_IP == "null") {
        RETOUR = "Auto";
      } else {
        RETOUR = SPIFFS_IP;
      }
    }

    if (var == "DARKCOLOR") {
      RETOUR = DarkColor;
    }

    if (var == "LIGHTCOLOR") {
      RETOUR = LightColor;
    }

    if (var == "ACTIVECOLOR") {
      RETOUR = ActiveColor;
    }

    if (var == "TEXTCOLOR") {
      RETOUR = TextColor;
    }

    if (var == "POWERED") {
      RETOUR = Powered;
    }

    return RETOUR;
  }
};
void MiraPanelServer::Debug(bool Value) {
  _Debug = Value;
}

void MiraPanelServer::miraLvgl(bool Value) {
  _LVGL = Value;
}

void MiraPanelServer::Jeedom(bool Value) {
  JEEDOM_mira = Value;
}

void MiraPanelServer::JeedomAutoRefresh(bool Value) {
  AUTO_REFRESH = Value;
}

void MiraPanelServer::GenericAutoRefresh(bool Value) {
  AUTO_REFRESH = Value;
}



void MiraPanelServer::IndexAddLittleLogo(String URL, int Width, int Height) {
  Little_Logo_URL = URL;

  if (Width > 110) {
    Width = 110;
  }

  if (Height > 110) {
    Height = 110;
  }
  Little_Logo_Width = Width;
  Little_Logo_Height = Height;
}

void MiraPanelServer::IndexAddLogo(String URL, int Width, int Height) {
  Logo_URL = URL;
  Logo_Width = Width;
  Logo_Height = Height;
}



void MiraPanelServer::IndexBottomText(String Text) {
  Powered = Text;
}


void MiraPanelServer::IndexDarkColor(String HTML_Color) {
  if (HTML_Color == "" || HTML_Color == "Defaut") {
    DarkColor = "#1E5064";
  } else {
    DarkColor = HTML_Color;
  }
}

void MiraPanelServer::IndexLightColor(String HTML_Color) {
  if (HTML_Color == "" || HTML_Color == "Defaut") {
    LightColor = "#7193A0";
  } else {
    LightColor = HTML_Color;
  }
}


void MiraPanelServer::IndexActiveColor(String HTML_Color) {
  if (HTML_Color == "" || HTML_Color == "Defaut") {
    ActiveColor = "#D39E55";
  } else {
    ActiveColor = HTML_Color;
  }
}

void MiraPanelServer::IndexTextColor(String HTML_Color) {
  if (HTML_Color == "" || HTML_Color == "Defaut") {
    TextColor = "#FFFFFF";
  } else {
    TextColor = HTML_Color;
  }
}

void MiraPanelServer::Scan_Network(bool Value) {
  _Scan = Value;
}

void MiraPanelServer::SetUser(char* ID_USER) {
  Ident_ID = ID_USER;
}


void MiraPanelServer::ServeurDNS() {

  if (_Debug == true) Serial.println(F("[MIRA] Démarrage du mode configuration Wi-Fi"));

  // Premier démarrage / flash effacé : repartir d'un état Wi-Fi propre.
  // Sur ESP32-S3, cela évite qu'un ancien état STA empêche parfois le softAP
  // d'apparaître immédiatement après un erase flash complet.
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(250);
  WiFi.mode(WIFI_OFF);
  delay(150);
  WiFi.mode(WIFI_AP);
  delay(150);

  const char* setupSsid = "Mira Panel";
  bool apOk = WiFi.softAP(setupSsid);
  if (!apOk) {
    if (_Debug) Serial.println(F("[MIRA][WIFI] Echec du point d'acces, nouvelle tentative..."));
    delay(300);
    WiFi.mode(WIFI_AP);
    apOk = WiFi.softAP(setupSsid);
  }

  if (_Debug) {
    Serial.print(F("[MIRA][WIFI] Point d'acces : "));
    Serial.println(apOk ? F("ACTIF") : F("ECHEC"));
    Serial.print(F("[MIRA][WIFI] SSID : "));
    Serial.println(setupSsid);
    Serial.print(F("[MIRA][WIFI] IP : "));
    Serial.println(WiFi.softAPIP());
  }
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send_P(200, "text/html", Page_Key, RetourVariable);
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->value().c_str()) == "wifi") {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
          if (p->name() == "ssid") SPIFFS_SSID = p->value().c_str();
          if (p->name() == "pass") SPIFFS_Pass = p->value().c_str();
          if (p->name() == "ip") SPIFFS_IP = p->value().c_str();
        }
      }
      String networkError;
      if (!miraReadNetworkForm(request,networkError)) {
        request->send(400,"text/plain",networkError);return;
      }
      if (SPIFFS_EnregistrementReseau()) {
        if (ModeAP == false) {
          request->send_P(200, "text/html", Page_Index, RetourVariable);

        } else {
          request->send_P(200, "text/html", Page_Enregistrement, RetourVariable);
          delay(1000);
          ESP.restart();
        }

      } else {
        request->send(200, "text/plain", "Erreur d'écriture de la configuration...");
      }
    } else if (String(p->value().c_str()) == "key") {
      int params = request->params();
      for (int i = 0; i < params; i++) {
        AsyncWebParameter* p = request->getParam(i);
        if (p->isPost()) {
          if (p->name() == "key") Uncrypt_Pass = p->value().c_str();
        }
      }
      if (SPIFFS_EnregistrementAP()) {
        request->send_P(200, "text/html", Page_ManageurWiFiSansR, RetourVariable);
      } else {
        request->send(200, "text/plain", "Erreur d'écriture de la configuration...");
      }
    }
  });

  server.on("/load", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);

    if (String(p->name().c_str()) == "nav") {
      if (String(p->value().c_str()) == "wifisr") {
        request->send_P(200, "text/html", Page_ManageurWiFiSansR, RetourVariable);
      } else if (String(p->value().c_str()) == "key") {
        request->send_P(200, "text/html", Page_Key, RetourVariable);
      } else if (String(p->value().c_str()) == "reboot") {
        SPIFFS_Config_Reseau["ap"][0]["actif"].as<String>() = "1";
        if (SPIFFS_EnregistrementAP()) {
          TITRE = "Enregistrement terminé";
          MESSAGE = "Redémarrage de l'appareil en point d'accès...";
          request->send_P(200, "text/html", Page_Message, RetourVariable);
          delay(1000);
          ESP.restart();
        }
      }
    }
  });

  dnsServer.start(53, "*", WiFi.softAPIP());
  server.addHandler(new GestionnairePortailCaptif()).setFilter(ON_AP_FILTER);
  DNS = true;
  server.begin();
}

String ReturnJSONModule() {
  String Retour_Json = "";
  serializeJsonPretty(SPIFFS_Config_Module, Retour_Json);
  return Retour_Json;
}



String ReturnJSONIndex() {
  String Retour_Json = "";
  serializeJsonPretty(SPIFFS_Config_Actions, Retour_Json);
  return Retour_Json;
}

String ReturnJSONlvgl() {
  String Retour_Json = "";
  serializeJsonPretty(mira_Json_LVGL, Retour_Json);
  return Retour_Json;
}


#define U_PART U_SPIFFS

size_t content_len;
static int miraManualUpdateLastPercent = -1;
static bool miraManualUpdateSucceeded = false;
static bool miraManualUpdateFailed = false;

void handleDoUpdate(AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
  if (!index) {
    if (_Debug) Serial.println("[MIRA][OTA] Televersement manuel demarre");
    miraManualUpdateLastPercent = -1;
    miraManualUpdateSucceeded = false;
    miraManualUpdateFailed = false;
    content_len = request->contentLength();

    int cmd = (filename.indexOf("spiffs") > -1) ? U_PART : U_FLASH;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd)) {
      Update.printError(Serial);
      miraManualUpdateFailed = true;
      if (_Debug) Serial.println("[MIRA][OTA] Echec initialisation Update");
    }
  }

  if (!miraManualUpdateFailed && len > 0) {
    if (Update.write(data, len) != len) {
      Update.printError(Serial);
      miraManualUpdateFailed = true;
      if (_Debug) Serial.println("[MIRA][OTA] Erreur ecriture firmware");
      if (_Debug) Serial.printf("[MIRA][OTA] Octets ecrits : %u\n", (unsigned)Update.progress());
    } else {
      int percent = 0;
      if (content_len > 0) {
        percent = (int)(((index + len) * 100ULL) / content_len);
        if (percent > 99 && !final) percent = 99;
      }
      if (final) percent = 100;

      if (percent != miraManualUpdateLastPercent) {
        miraManualUpdateLastPercent = percent;
        if (_Debug) Serial.printf("[MIRA][OTA] Progression : %d%%\n", percent);
      }
    }
  }

  if (final) {
    if (!miraManualUpdateFailed && !Update.hasError() && Update.end(true)) {
      miraManualUpdateSucceeded = true;
      if (_Debug) Serial.println("[MIRA][OTA] Televersement termine");
    } else {
      miraManualUpdateFailed = true;
      miraManualUpdateSucceeded = false;
      Update.printError(Serial);
      if (Update.isRunning()) Update.abort();
      if (_Debug) Serial.println("[MIRA][OTA] Televersement manuel abandonne");
    }
  }
}



// Diagnostics for UI navigation saturation. Counts only real page loads,
// not polling; this intentionally does not allocate a JsonDocument or use
// the heavy HTML response templating path.
static volatile uint32_t miraNavCount = 0;
static volatile uint32_t miraNavLastHeap = 0;
static volatile uint32_t miraNavLowestHeap = 0xffffffffU;

static void miraObserveNavigation(const char* page) {
  ++miraNavCount;
  const uint32_t heap = ESP.getFreeHeap();
  miraNavLastHeap = heap;
  if (heap < miraNavLowestHeap) miraNavLowestHeap = heap;
  Serial.printf("[MIRA][WEB] %s nav=%u heap=%u min=%u largest=%u\n",
                page, (unsigned)miraNavCount, (unsigned)heap,
                (unsigned)ESP.getMinFreeHeap(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

void MiraPanelServer::ServeurStation() {
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
  MiraWT32GPIO::begin();
  auto miraGpioAuthorize = [](AsyncWebServerRequest* request) -> bool {
    char pass[50]; Uncrypt_Pass.toCharArray(pass, sizeof(pass));
    if (!request->authenticate(Ident_ID, pass)) {
      request->requestAuthentication(); return false;
    }
    return true;
  };

  server.on("/gpio/multi/config", HTTP_POST, [miraGpioAuthorize](AsyncWebServerRequest* request) {
    if(!miraGpioAuthorize(request))return;
    if(!request->hasHeader("X-Mira-GPIO") ||
       !request->hasParam("name",true) || !request->hasParam("gpio",true) ||
       !request->hasParam("visible",true)) {
      request->send(400,"text/plain","Parametres manquants");return;
    }
    String name=request->getParam("name",true)->value();
    String gpio=request->getParam("gpio",true)->value();
    String vis=request->getParam("visible",true)->value();
    if(!gpio.length() || gpio.length()>2 || gpio!=String(gpio.toInt()) ||
       (vis!="0" && vis!="1")) {
      request->send(400,"text/plain","Parametres invalides");return;
    }
    bool reload=false;
    if(!MiraWT32GPIO::multiConfigure(name,gpio.toInt(),vis=="1",reload)) {
      request->send(409,"text/plain","GPIO de sortie ou deja utilise");return;
    }
    // Defer screen reconstruction to the main loop. Never touch LVGL here.
    if(reload) paramEvent=MIRA_SAVE_LIST_LVGL;
    request->send(200,"text/plain","OK");
  });

  server.on("/gpio/link", HTTP_POST, [miraGpioAuthorize](AsyncWebServerRequest* request) {
    if (!miraGpioAuthorize(request)) return;
    if (!request->hasHeader("X-Mira-GPIO") ||
        !request->hasParam("name", true) || !request->hasParam("gpio", true) ||
        !request->hasParam("source", true) || !request->hasParam("level", true)) {
      request->send(400, "text/plain", "Parametres incomplets"); return;
    }
    String name=request->getParam("name",true)->value();
    String g=request->getParam("gpio",true)->value();
    String source=request->getParam("source",true)->value();
    String level=request->getParam("level",true)->value();
    if ((source!="jeedom" && source!="gpio") ||
        !g.length() || g.length()>2 || g!=String(g.toInt()) ||
        (level!="high" && level!="low" && level!="read")) {
      request->send(400, "text/plain", "Parametres incorrects"); return;
    }
    int gpio = source=="gpio" ? g.toInt() : 0;
    int kind = level=="high" ? 1 : (level=="low" ? 0 : 2);
    if (!MiraWT32GPIO::saveLink(name,gpio,kind)) {
      request->send(409, "text/plain", "GPIO non configure en sortie ou table pleine"); return;
    }
    request->send(200, "text/plain", "OK");
  });
  server.on("/gpio/raw", HTTP_POST, [miraGpioAuthorize](AsyncWebServerRequest* request) {
    if (!miraGpioAuthorize(request)) return;
    if (!request->hasHeader("X-Mira-GPIO") ||
        !request->hasParam("gpio", true) || !request->hasParam("level", true)) {
      request->send(400, "text/plain", "Parametres manquants"); return;
    }
    String g=request->getParam("gpio",true)->value();
    String level=request->getParam("level",true)->value();
    if (!g.length() || g.length()>2 || g!=String(g.toInt()) ||
        (level!="high" && level!="low")) {
      request->send(400, "text/plain", "Parametres incorrects"); return;
    }
    int idx=MiraWT32GPIO::findPin(g.toInt());
    if (idx<0 || !MiraWT32GPIO::writePhysical(uint8_t(idx),level=="high")) {
      request->send(409,"text/plain","Sortie GPIO indisponible"); return;
    }
    request->send(200, "text/plain", "OK");
  });
  server.on("/gpio/state", HTTP_GET, [miraGpioAuthorize](AsyncWebServerRequest* request) {
    if (!miraGpioAuthorize(request)) return;
    AsyncWebServerResponse* response =
        request->beginResponse(200, "application/json", MiraWT32GPIO::stateJson());
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });
  server.on("/gpio/config", HTTP_POST, [miraGpioAuthorize](AsyncWebServerRequest* request) {
    if (!miraGpioAuthorize(request)) return;
    // Disallow simple cross-origin HTML forms controlling a GPIO.
    if (!request->hasHeader("X-Mira-GPIO") ||
        !request->hasParam("gpio", true) || !request->hasParam("mode", true) ||
        !request->hasParam("inverted", true) || !request->hasParam("boot", true)) {
      request->send(400, "text/plain", "Parametres GPIO manquants"); return;
    }
    const String g = request->getParam("gpio", true)->value();
    const String m = request->getParam("mode", true)->value();
    const String inv = request->getParam("inverted", true)->value();
    const String boot = request->getParam("boot", true)->value();
    if (!g.length() || g.length() > 2 || !g.equals(String(g.toInt())) ||
        (inv != "0" && inv != "1")) {
      request->send(400, "text/plain", "GPIO invalide"); return;
    }
    const int i = MiraWT32GPIO::findPin(g.toInt());
    const int parsed = MiraWT32GPIO::parseMode(m);
    const int startup = MiraWT32GPIO::parseBoot(boot);
    if (i < 0 || parsed < 0 || startup < 0) {
      request->send(400, "text/plain", "GPIO ou mode interdit"); return;
    }
    if (!MiraWT32GPIO::configure((uint8_t)i, (uint8_t)parsed, inv == "1", (uint8_t)startup)) {
      request->send(503, "text/plain", "Erreur enregistrement NVS"); return;
    }
    request->send(200, "text/plain", "OK");
  });
  server.on("/gpio/set", HTTP_POST, [miraGpioAuthorize](AsyncWebServerRequest* request) {
    if (!miraGpioAuthorize(request)) return;
    if (!request->hasHeader("X-Mira-GPIO") ||
        !request->hasParam("gpio", true) || !request->hasParam("state", true)) {
      request->send(400, "text/plain", "Parametres GPIO manquants"); return;
    }
    const String g = request->getParam("gpio", true)->value();
    const String state = request->getParam("state", true)->value();
    if (!g.length() || g.length() > 2 || !g.equals(String(g.toInt())) ||
        (state != "0" && state != "1")) {
      request->send(400, "text/plain", "GPIO ou niveau invalide"); return;
    }
    const int i = MiraWT32GPIO::findPin(g.toInt());
    if (i < 0 || !MiraWT32GPIO::write((uint8_t)i, state == "1")) {
      request->send(409, "text/plain", "Broche non configuree en sortie"); return;
    }
    request->send(200, "text/plain", "OK");
  });
#endif
  server.on("/mira-health", HTTP_GET, [](AsyncWebServerRequest* request) {
    char body[440];
    snprintf(body, sizeof(body),
      "{\"uptime_ms\":%lu,\"wifi\":%d,\"rssi_dbm\":%d,\"heap_free\":%u,"
      "\"heap_min\":%u,\"heap_largest_block\":%u,\"psram_free\":%u,"
      "\"nav_count\":%u,\"nav_last_heap\":%u,\"nav_lowest_heap\":%u,"
      "\"internal_heap_free\":%u,\"internal_largest_block\":%u}",
      (unsigned long)millis(), (int)WiFi.status(), (int)WiFi.RSSI(),
      (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
      (unsigned)ESP.getFreePsram(), (unsigned)miraNavCount,
      (unsigned)miraNavLastHeap, (unsigned)miraNavLowestHeap,
      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    AsyncWebServerResponse* response =
        request->beginResponse(200, "application/json", body);
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    char CPass[50];
    Uncrypt_Pass.toCharArray(CPass, 50);
    if (!request->authenticate(Ident_ID, CPass))
      return request->requestAuthentication();
    miraObserveNavigation("accueil");
    request->send_P(200, "text/html", Page_Index, RetourVariable);
  });
  //server.serveStatic("/", SPIFFS, "/");

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest* request) {
    Resend = true;
    ActionGet = false;
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "send") {
      Action = String(p->value().c_str()).toInt();

    } else if (String(p->name().c_str()) == "send_color") {
      Action = String(p->value().c_str()).toInt();

      String SNom = String(p->value().c_str());
      String ID = String(p->value().c_str());
      JsonArray Actions = SPIFFS_Config_Actions["actions"];
      for (int i = 0; i < Actions.size(); i++) {
        if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID) {
          p = request->getParam(1);
          int R = (String(p->value().c_str())).toInt();
          SPIFFS_Config_Actions["actions"][i]["r"] = String(p->value().c_str());
          p = request->getParam(2);
          int G = (String(p->value().c_str())).toInt();
          SPIFFS_Config_Actions["actions"][i]["g"] = String(p->value().c_str());
          p = request->getParam(3);
          int B = (String(p->value().c_str())).toInt();
          SPIFFS_Config_Actions["actions"][i]["b"] = String(p->value().c_str());

          char CNom[50];
          SNom.toCharArray(CNom, 50);
          long r = 0;
          r = (long)R;
          long g = 0;
          g = (long)G;
          long b = 0;
          b = (long)B;

          String SValue = "#000000";
          char CValue[7];
          sprintf(CValue, "#%02x%02x%02x", r, g, b);

          events.send(CValue, CNom, millis());

          break;
        }
      }

    } else if (String(p->name().c_str()) == "send_range") {
      Action = String(p->value().c_str()).toInt();
      String ID = String(p->value().c_str());
      JsonArray Actions = SPIFFS_Config_Actions["actions"];
      for (int i = 0; i < Actions.size(); i++) {
        if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID) {
          String SNom = String(p->value().c_str());
          p = request->getParam(1);
          SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());

          char CNom[50];
          SNom.toCharArray(CNom, 50);
          char CValue[50];
          String SValue = String(p->value().c_str());
          SValue.toCharArray(CValue, 50);
          events.send(CValue, CNom, millis());

          break;
        }
      }
    } else if (String(p->name().c_str()) == "send_list") {
      Action = String(p->value().c_str()).toInt();
      String ID = String(p->value().c_str());
      p = request->getParam(1);
      String SNom = String(p->value().c_str());
      char CNom[50];
      SNom.toCharArray(CNom, 50);
      char CValue[50];
      String SValue = ID;
      SValue.toCharArray(CValue, 50);
      events.send(CNom, CValue, millis());
      for (int k = 0; k < NB_Listes; k++) {
        if (Listes[k] == SNom) {
          Listes_Value[k] = SValue;
          break;
        }
      }

      JsonArray Actions = SPIFFS_Config_Actions["actions"];
      for (int j = 0; j < Actions.size(); j++) {
        if (SValue == SPIFFS_Config_Actions["actions"][j]["id"].as<String>()) {
          String Value = SPIFFS_Config_Actions["actions"][j]["id"].as<String>();
          for (int i = 0; i < Actions.size(); i++) {
            if (SNom == SPIFFS_Config_Actions["actions"][i]["nomliste"].as<String>()) {
              SPIFFS_Config_Actions["actions"][i]["value"] = Value;
            }
          }
          break;
        }
      }


    } else if (String(p->name().c_str()) == "send_textbox") {
      Action = String(p->value().c_str()).toInt();
      String ID = String(p->value().c_str());
      JsonArray Actions = SPIFFS_Config_Actions["actions"];
      for (int i = 0; i < Actions.size(); i++) {
        if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID) {
          p = request->getParam(1);
          SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
          String SNom = String(p->value().c_str());

          char CNom[50];
          SNom.toCharArray(CNom, 50);

          char CValue[50];
          String SValue = ID;
          SValue.toCharArray(CValue, 50);
          events.send(CNom, CValue, millis());
          break;
        }
      }

    } else if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {

      AsyncWebParameter* p = request->getParam(1);

      if (String(p->name().c_str()) == "button") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "button") {
            if (String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id"].as<String>()) {
              Action = String(p->value().c_str()).toInt();
              request->send(200, "text/plain", String(Action));
              break;
            }
          }
        }
      } else if (String(p->name().c_str()) == "httpaction") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "httpaction") {
            if (String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id"].as<String>()) {
              Action = String(p->value().c_str()).toInt();

              request->send(200, "text/plain", String(Action));
              break;
            }
          }
        }
      } else if (String(p->name().c_str()) == "toogle" || String(p->name().c_str()) == "toggle") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "toggle") {
            if (String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() || String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id1"].as<String>()) {
              Action = String(p->value().c_str()).toInt();
              request->send(200, "text/plain", String(Action));
              break;
            }
          }
        }
      } else if (String(p->name().c_str()) == "info") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          Action = String(p->value().c_str()).toInt();

          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {
            String SNom = String(p->value().c_str());
            char CNom[50];
            SNom.toCharArray(CNom, 50);
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
            String SValue = String(p->value().c_str());
            char CValue[50];
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "inputbox") {
        Action = String(p->value().c_str()).toInt();

        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {

          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {
            String SNom = String(p->value().c_str());
            char CNom[50];
            SNom.toCharArray(CNom, 50);
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
            String SValue = String(p->value().c_str());
            char CValue[50];
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "infofa") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          Action = String(p->value().c_str()).toInt();

          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {
            String SNom = String(p->value().c_str());
            char CNom[50];
            SNom.toCharArray(CNom, 50);
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
            String SValue = String(p->value().c_str());
            char CValue[50];
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "color") {
        Action = String(p->value().c_str()).toInt();
        String ID = String(p->value().c_str());
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID) {
            String SNom = String(p->value().c_str());
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["r"] = String(p->value().c_str());
            int R = (String(p->value().c_str())).toInt();
            p = request->getParam(3);
            SPIFFS_Config_Actions["actions"][i]["g"] = String(p->value().c_str());
            int G = (String(p->value().c_str())).toInt();
            p = request->getParam(4);
            SPIFFS_Config_Actions["actions"][i]["b"] = String(p->value().c_str());
            int B = (String(p->value().c_str())).toInt();

            char CNom[50];
            SNom.toCharArray(CNom, 50);
            long r = 0;
            r = (long)R;
            long g = 0;
            g = (long)G;
            long b = 0;
            b = (long)B;

            String SValue = "#000000";
            char CValue[7];
            sprintf(CValue, "#%02x%02x%02x", r, g, b);

            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "range") {
        Action = String(p->value().c_str()).toInt();
        String ID = String(p->value().c_str());
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID) {
            String SNom = String(p->value().c_str());
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());

            char CNom[50];
            SNom.toCharArray(CNom, 50);
            char CValue[50];
            String SValue = String(p->value().c_str());
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            break;
          }
        }
      } else if (String(p->name().c_str()) == "list") {
        String Nom_Liste = String(p->value().c_str());

        for (int k = 0; k < NB_Listes; k++) {
          if (Listes[k] == Nom_Liste) {
            p = request->getParam(2);
            Listes_Value[k] = String(p->value().c_str());

            char CNom[50];
            Nom_Liste.toCharArray(CNom, 50);
            char CValue[50];
            String SValue = String(p->value().c_str());
            SValue.toCharArray(CValue, 50);
            events.send(CNom, CValue, millis());
            Action = String(p->value().c_str()).toInt();

            JsonArray Actions = SPIFFS_Config_Actions["actions"];
            for (int j = 0; j < Actions.size(); j++) {
              if (Nom_Liste == SPIFFS_Config_Actions["actions"][j]["nomliste"].as<String>()) {
                SPIFFS_Config_Actions["actions"][j]["value"] = SValue;
              }
            }


            request->send(200, "text/plain", SValue);
            break;
          }
        }
      } else if (String(p->name().c_str()) == "param") {
        SPIFFS_ADD_Param = String(p->value().c_str());
        p = request->getParam(2);
        SPIFFS_ADD_Value = String(p->value().c_str());
        SPIFFS_UpdateParam();
        request->send(200, "text/plain", "Paramètre mis à jour");
      }
    }
  });



  server.on("/update", HTTP_GET, [](AsyncWebServerRequest* request) {
    Resend = true;
    ActionGet = true;
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {

      AsyncWebParameter* p = request->getParam(1);

      if (String(p->name().c_str()) == "button") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "button") {
            if (String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id"].as<String>()) {
              Action = String(p->value().c_str()).toInt();
              request->send(200, "text/plain", String(Action));
              break;
            }
          }
        }
      } else if (String(p->name().c_str()) == "httpaction") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "httpaction") {
            if (String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id"].as<String>()) {
              Action = String(p->value().c_str()).toInt();

              request->send(200, "text/plain", String(Action));
              break;
            }
          }
        }
      } else if (String(p->name().c_str()) == "toogle" || String(p->name().c_str()) == "toggle") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "toggle") {
            if (String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() || String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id1"].as<String>()) {
              Action = String(p->value().c_str()).toInt();
              request->send(200, "text/plain", String(Action));
              break;
            }
          }
        }
      } else if (String(p->name().c_str()) == "info") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {

          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {
            Action = String(p->value().c_str()).toInt();

            String SNom = String(p->value().c_str());
            char CNom[50];
            SNom.toCharArray(CNom, 50);
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
            String SValue = String(p->value().c_str());
            char CValue[50];
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "infofa") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {

          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {
            Action = String(p->value().c_str()).toInt();

            String SNom = String(p->value().c_str());
            char CNom[50];
            SNom.toCharArray(CNom, 50);
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
            String SValue = String(p->value().c_str());
            char CValue[50];
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "inputbox") {
        Action = String(p->value().c_str()).toInt();

        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {

          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {
            String SNom = String(p->value().c_str());
            char CNom[50];
            SNom.toCharArray(CNom, 50);
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
            String SValue = String(p->value().c_str());
            char CValue[50];
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "color") {
        Action = String(p->value().c_str()).toInt();

        String ID = String(p->value().c_str());
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID) {
            String SNom = String(p->value().c_str());
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["r"] = String(p->value().c_str());
            int R = (String(p->value().c_str())).toInt();
            p = request->getParam(3);
            SPIFFS_Config_Actions["actions"][i]["g"] = String(p->value().c_str());
            int G = (String(p->value().c_str())).toInt();
            p = request->getParam(4);
            SPIFFS_Config_Actions["actions"][i]["b"] = String(p->value().c_str());
            int B = (String(p->value().c_str())).toInt();

            char CNom[50];
            SNom.toCharArray(CNom, 50);
            long r = 0;
            r = (long)R;
            long g = 0;
            g = (long)G;
            long b = 0;
            b = (long)B;

            String SValue = "#000000";
            char CValue[7];
            sprintf(CValue, "#%02x%02x%02x", r, g, b);

            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "range") {
        Action = String(p->value().c_str()).toInt();

        String ID = String(p->value().c_str());
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID) {
            String SNom = String(p->value().c_str());
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());

            char CNom[50];
            SNom.toCharArray(CNom, 50);
            char CValue[50];
            String SValue = String(p->value().c_str());
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", SValue);
            break;
          }
        }
      } else if (String(p->name().c_str()) == "list") {
        String Nom_Liste = String(p->value().c_str());
        for (int k = 0; k < NB_Listes; k++) {
          if (Listes[k] == Nom_Liste) {
            p = request->getParam(2);
            Listes_Value[k] = String(p->value().c_str());

            char CNom[50];
            Nom_Liste.toCharArray(CNom, 50);
            char CValue[50];
            String SValue = String(p->value().c_str());
            SValue.toCharArray(CValue, 50);
            events.send(CNom, CValue, millis());
            Action = String(p->value().c_str()).toInt();

            JsonArray Actions = SPIFFS_Config_Actions["actions"];
            for (int j = 0; j < Actions.size(); j++) {
              if (Nom_Liste == SPIFFS_Config_Actions["actions"][j]["nomliste"].as<String>()) {
                SPIFFS_Config_Actions["actions"][j]["value"] = SValue;
              }
            }


            request->send(200, "text/plain", SValue);
            break;
          }
        }
      } else if (String(p->name().c_str()) == "listvalue") {
        String Nom_Liste = String(p->value().c_str());
        for (int k = 0; k < NB_Listes; k++) {
          if (Listes[k] == Nom_Liste) {
            p = request->getParam(2);
            String SValue = String(p->value().c_str());

            JsonArray Actions = SPIFFS_Config_Actions["actions"];
            for (int j = 0; j < Actions.size(); j++) {

              if (Nom_Liste == SPIFFS_Config_Actions["actions"][j]["nomliste"].as<String>() && SValue == SPIFFS_Config_Actions["actions"][j]["nom"].as<String>()) {


                SValue = SPIFFS_Config_Actions["actions"][j]["id"].as<String>();
                Listes_Value[k] = SValue;
                char CNom[50];
                Nom_Liste.toCharArray(CNom, 50);
                char CValue[50];

                SValue.toCharArray(CValue, 50);
                events.send(CNom, CValue, millis());
                Action = SValue.toInt();

                for (int l = 0; l < Actions.size(); l++) {
                  if (Nom_Liste == SPIFFS_Config_Actions["actions"][l]["nomliste"].as<String>()) {
                    SPIFFS_Config_Actions["actions"][l]["value"] = SPIFFS_Config_Actions["actions"][j]["nom"].as<String>();
                  }
                }
                request->send(200, "text/plain", SValue);
                break;
              }
              if (j == Actions.size() - 1) {
                request->send(200, "text/plain", "Erreur");
              }
            }
          }
        }
      } else if (String(p->name().c_str()) == "param") {
        SPIFFS_ADD_Param = String(p->value().c_str());
        p = request->getParam(2);
        SPIFFS_ADD_Value = String(p->value().c_str());
        SPIFFS_UpdateParam();
        request->send(200, "text/plain", "Paramètre mis à jour");
      }
    }
  });

  // Jeedom may replay an ON/OFF state after a WT32 restart. This endpoint
  // changes only the screen's stored toggle state and LVGL presentation:
  // it never touches Action, JeedomCmd or any real actuator.
  server.on("/sync/toggle", HTTP_GET, [this](AsyncWebServerRequest* request) {
    AsyncWebParameter* key = request->getParam("key");
    AsyncWebParameter* idParam = request->getParam("id");
    if (!key || key->value() != Uncrypt_Pass) {
      request->send(403, "text/plain", "Cle invalide"); return;
    }
    if (!idParam) {
      request->send(400, "text/plain", "ID manquant"); return;
    }
    String idText = idParam->value();
    if (idText.isEmpty() || idText.length() > 6) {
      request->send(400, "text/plain", "ID invalide"); return;
    }
    for (size_t i = 0; i < idText.length(); ++i) {
      if (idText[i] < '0' || idText[i] > '9') {
        request->send(400, "text/plain", "ID invalide"); return;
      }
    }
    const int toggleId = idText.toInt();
    if (toggleId < 1) {
      request->send(400, "text/plain", "ID invalide"); return;
    }
    bool found = false;
    JsonArray actions = SPIFFS_Config_Actions["actions"];
    for (int i = 0; i < actions.size(); ++i) {
      if (actions[i]["type"].as<String>() != "toggle") continue;
      if (actions[i]["id0"].as<String>() == idText ||
          actions[i]["id1"].as<String>() == idText) {
        found = true;
        break;
      }
    }
    if (!found) {
      request->send(404, "text/plain", "Interrupteur inconnu"); return;
    }
    const uint8_t nextTail =
        uint8_t((_passiveToggleTail + 1) % PASSIVE_TOGGLE_QUEUE_SIZE);
    if (nextTail == _passiveToggleHead) {
      request->send(503, "text/plain", "File d'etats pleine"); return;
    }
    IndexSetToggle(toggleId);
    _passiveToggleEvents[_passiveToggleTail] = toggleId;
    _passiveToggleTail = nextTail;
    request->send(200, "text/plain", "OK");
  });

  server.on("/updatenr", HTTP_GET, [](AsyncWebServerRequest* request) {
    Resend = false;
    ActionGet = true;
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {

      AsyncWebParameter* p = request->getParam(1);

      if (String(p->name().c_str()) == "button") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "button") {
            if (String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id"].as<String>()) {
              Action = String(p->value().c_str()).toInt();
              request->send(200, "text/plain", String(Action));
              break;
            }
          }
        }
      } else if (String(p->name().c_str()) == "httpaction") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "httpaction") {
            if (String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id"].as<String>()) {
              Action = String(p->value().c_str()).toInt();

              request->send(200, "text/plain", String(Action));
              break;
            }
          }
        }
      } else if (String(p->name().c_str()) == "toogle" || String(p->name().c_str()) == "toggle") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "toggle") {
            if (String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() || String(p->value().c_str()) == SPIFFS_Config_Actions["actions"][i]["id1"].as<String>()) {
              Action = String(p->value().c_str()).toInt();
              request->send(200, "text/plain", String(Action));
              break;
            }
          }
        }
      } else if (String(p->name().c_str()) == "info") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {

          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {
            Action = String(p->value().c_str()).toInt();

            String SNom = String(p->value().c_str());
            char CNom[50];
            SNom.toCharArray(CNom, 50);
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
            String SValue = String(p->value().c_str());
            char CValue[50];
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "infofa") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {

          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {
            Action = String(p->value().c_str()).toInt();

            String SNom = String(p->value().c_str());
            char CNom[50];
            SNom.toCharArray(CNom, 50);
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
            String SValue = String(p->value().c_str());
            char CValue[50];
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "inputbox") {
        Action = String(p->value().c_str()).toInt();

        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {

          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {
            String SNom = String(p->value().c_str());
            char CNom[50];
            SNom.toCharArray(CNom, 50);
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());
            String SValue = String(p->value().c_str());
            char CValue[50];
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "color") {
        Action = String(p->value().c_str()).toInt();

        String ID = String(p->value().c_str());
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID) {
            String SNom = String(p->value().c_str());
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["r"] = String(p->value().c_str());
            int R = (String(p->value().c_str())).toInt();
            p = request->getParam(3);
            SPIFFS_Config_Actions["actions"][i]["g"] = String(p->value().c_str());
            int G = (String(p->value().c_str())).toInt();
            p = request->getParam(4);
            SPIFFS_Config_Actions["actions"][i]["b"] = String(p->value().c_str());
            int B = (String(p->value().c_str())).toInt();

            char CNom[50];
            SNom.toCharArray(CNom, 50);
            long r = 0;
            r = (long)R;
            long g = 0;
            g = (long)G;
            long b = 0;
            b = (long)B;

            String SValue = "#000000";
            char CValue[7];
            sprintf(CValue, "#%02x%02x%02x", r, g, b);

            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", "OK");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "range") {
        Action = String(p->value().c_str()).toInt();

        String ID = String(p->value().c_str());
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID) {
            String SNom = String(p->value().c_str());
            p = request->getParam(2);
            SPIFFS_Config_Actions["actions"][i]["value"] = String(p->value().c_str());

            char CNom[50];
            SNom.toCharArray(CNom, 50);
            char CValue[50];
            String SValue = String(p->value().c_str());
            SValue.toCharArray(CValue, 50);
            events.send(CValue, CNom, millis());
            request->send(200, "text/plain", SValue);
            break;
          }
        }
      } else if (String(p->name().c_str()) == "list") {
        String Nom_Liste = String(p->value().c_str());
        for (int k = 0; k < NB_Listes; k++) {
          if (Listes[k] == Nom_Liste) {
            p = request->getParam(2);
            Listes_Value[k] = String(p->value().c_str());

            char CNom[50];
            Nom_Liste.toCharArray(CNom, 50);
            char CValue[50];
            String SValue = String(p->value().c_str());
            SValue.toCharArray(CValue, 50);
            events.send(CNom, CValue, millis());
            Action = String(p->value().c_str()).toInt();

            JsonArray Actions = SPIFFS_Config_Actions["actions"];
            for (int j = 0; j < Actions.size(); j++) {
              if (Nom_Liste == SPIFFS_Config_Actions["actions"][j]["nomliste"].as<String>()) {
                SPIFFS_Config_Actions["actions"][j]["value"] = SValue;
              }
            }


            request->send(200, "text/plain", SValue);
            break;
          }
        }
      } else if (String(p->name().c_str()) == "listvalue") {
        String Nom_Liste = String(p->value().c_str());
        for (int k = 0; k < NB_Listes; k++) {
          if (Listes[k] == Nom_Liste) {
            p = request->getParam(2);
            String SValue = String(p->value().c_str());

            JsonArray Actions = SPIFFS_Config_Actions["actions"];
            for (int j = 0; j < Actions.size(); j++) {

              if (Nom_Liste == SPIFFS_Config_Actions["actions"][j]["nomliste"].as<String>() && SValue == SPIFFS_Config_Actions["actions"][j]["nom"].as<String>()) {


                SValue = SPIFFS_Config_Actions["actions"][j]["id"].as<String>();
                Listes_Value[k] = SValue;
                char CNom[50];
                Nom_Liste.toCharArray(CNom, 50);
                char CValue[50];

                SValue.toCharArray(CValue, 50);
                events.send(CNom, CValue, millis());
                Action = SValue.toInt();

                for (int l = 0; l < Actions.size(); l++) {
                  if (Nom_Liste == SPIFFS_Config_Actions["actions"][l]["nomliste"].as<String>()) {
                    SPIFFS_Config_Actions["actions"][l]["value"] = SPIFFS_Config_Actions["actions"][j]["nom"].as<String>();
                  }
                }
                request->send(200, "text/plain", SValue);
                break;
              }
              if (j == Actions.size() - 1) {
                request->send(200, "text/plain", "Erreur");
              }
            }
          }
        }
      } else if (String(p->name().c_str()) == "param") {
        SPIFFS_ADD_Param = String(p->value().c_str());
        p = request->getParam(2);
        SPIFFS_ADD_Value = String(p->value().c_str());
        SPIFFS_UpdateParam();
        request->send(200, "text/plain", "Paramètre mis à jour");
      }
    }
  });


  server.on("/display-save", HTTP_POST, [this](AsyncWebServerRequest* request) {
    auto postValue = [request](const char* name) -> String {
      AsyncWebParameter* p = request->getParam(name, true);
      if (!p) return String();
      String value = p->value();
      value.trim();
      return value;
    };

    auto normalizeClockValue = [](String value, String fallback) -> String {
      value.trim();
      value.replace("%3A", ":");
      value.replace("%3a", ":");
      if (value.length() == 4 && value.indexOf(':') < 0) {
        value = value.substring(0, 2) + ":" + value.substring(2, 4);
      }
      auto valid = [](const String& v) -> bool {
        if (v.length() != 5 || v[2] != ':') return false;
        if (!isDigit(v[0]) || !isDigit(v[1]) || !isDigit(v[3]) || !isDigit(v[4])) return false;
        int h = v.substring(0, 2).toInt();
        int m = v.substring(3, 5).toInt();
        return h >= 0 && h <= 23 && m >= 0 && m <= 59;
      };
      if (valid(value)) return value;
      fallback.trim();
      fallback.replace("%3A", ":");
      fallback.replace("%3a", ":");
      if (fallback.length() == 4 && fallback.indexOf(':') < 0) {
        fallback = fallback.substring(0, 2) + ":" + fallback.substring(2, 4);
      }
      return valid(fallback) ? fallback : String("00:00");
    };

    auto boundedInt = [](String value, int fallback, int minValue, int maxValue) -> int {
      value.trim();
      if (!value.length()) return fallback;
      for (size_t i = 0; i < value.length(); ++i) {
        if (!isDigit(value[i]) && !(i == 0 && value[i] == '-')) return fallback;
      }
      long parsed = value.toInt();
      if (parsed < minValue) parsed = minValue;
      if (parsed > maxValue) parsed = maxValue;
      return (int)parsed;
    };

    int currentDay = GetParamText("DISPLAY_Jour").toInt();
    int currentNight = GetParamText("DISPLAY_Nuit").toInt();
    int currentSleep = GetParamText("DISPLAY_Veille").toInt();
    int currentTimeout = GetParamText("DISPLAY_Delai").toInt();
    if (currentDay < 1 || currentDay > 100) currentDay = 85;
    if (currentNight < 1 || currentNight > 100) currentNight = 20;
    if (currentSleep < 0 || currentSleep > 100) currentSleep = 8;
    if (currentTimeout < 0 || currentTimeout > 86400) currentTimeout = 120;

    const int day = boundedInt(postValue("DISPLAY_Jour"), currentDay, 1, 100);
    const int night = boundedInt(postValue("DISPLAY_Nuit"), currentNight, 1, 100);
    const int sleepBrightness = boundedInt(postValue("DISPLAY_Veille"), currentSleep, 0, 100);
    const int sleepTimeout = boundedInt(postValue("DISPLAY_Delai"), currentTimeout, 0, 86400);
    const String start = normalizeClockValue(postValue("DISPLAY_DebutNuit"), GetParamText("DISPLAY_DebutNuit"));
    const String end = normalizeClockValue(postValue("DISPLAY_FinNuit"), GetParamText("DISPLAY_FinNuit"));
    const bool nightAuto = request->hasParam("DISPLAY_NuitAuto_Check", true);
    const bool clockSleep = request->hasParam("DISPLAY_HorlogeVeille_Check", true);

    auto updateTextIfChanged = [this](const char* name, const String& value) {
      if (GetParamText(name) != value) UpdateParamText(name, value);
    };
    auto updateBoolIfChanged = [this](const char* name, bool value) {
      if (GetParamBool(name) != value) UpdateParamBool(name, value);
    };

    updateTextIfChanged("DISPLAY_Jour", String(day));
    updateTextIfChanged("DISPLAY_Nuit", String(night));
    updateTextIfChanged("DISPLAY_Veille", String(sleepBrightness));
    updateTextIfChanged("DISPLAY_Delai", String(sleepTimeout));
    updateTextIfChanged("DISPLAY_DebutNuit", start);
    updateTextIfChanged("DISPLAY_FinNuit", end);
    updateBoolIfChanged("DISPLAY_NuitAuto", nightAuto);
    updateBoolIfChanged("DISPLAY_HorlogeVeille", clockSleep);

    Serial.printf("[MIRA][DISPLAY] Web save jour=%d%% nuit=%d%% veille=%d%% delai=%ds auto=%s %s-%s horloge=%s\n",
                  day, night, sleepBrightness, sleepTimeout,
                  nightAuto ? "oui" : "non", start.c_str(), end.c_str(),
                  clockSleep ? "oui" : "non");
    request->redirect("/load?nav=params");
  });

  server.on("/save", HTTP_POST, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "config") {

      if (String(p->value().c_str()) == "params") {

        int params = request->params();
        int emplacement = 0;
        NB_List_Param = 0;
        NB_List_lvgl = 0;

        JsonArray Json_Array = mira_Json_LVGL["lvgl"];
        bool lvgl = false;
        for (int i = 0; i < params; i++) {
          lvgl = false;
          AsyncWebParameter* p = request->getParam(i);
          if (p->isPost()) {
            for (int j = 0; j < Json_Array.size(); j++) {
              String nom_url = mira_Json_LVGL["lvgl"][j]["name"].as<String>();
              if (String(p->name().c_str()) == nom_url) lvgl = true;
            }
            if (!lvgl) {
              JsonArray Params = SPIFFS_Config_Params["params"];
              for (int j = 0; j < Params.size(); j++) {
                if (SPIFFS_Config_Params["params"][j]["param"].as<String>() == p->name().c_str()) {
                  if (SPIFFS_Config_Params["params"][j]["value"].as<String>() != p->value().c_str()) {
                    SPIFFS_Config_Params["params"][j]["value"] = p->value().c_str();
                    NB_List_Param++;
                  }
                }
              }
            }
          }
        }


        lvgl = false;
        for (int i = 0; i < params; i++) {
          AsyncWebParameter* p = request->getParam(i);
          if (p->isPost()) {
            for (int j = 0; j < Json_Array.size(); j++) {
              String nom_url = mira_Json_LVGL["lvgl"][j]["name"].as<String>();
              if (String(p->name().c_str()) == nom_url) {
                if (mira_Json_LVGL["lvgl"][j]["url"] != p->value().c_str()) {
                  mira_Json_LVGL["lvgl"][j]["url"] = p->value();
                  NB_List_lvgl++;
                }
              }
            }
          }
        }

        if (NB_List_Param > 0) SPIFFS_SaveParam_List();
        if (NB_List_lvgl > 0) SaveListLVGL();
        request->send_P(200, "text/html", Page_Index, RetourVariable);

      } else if (String(p->value().c_str()) == "AP") {

        int params = request->params();
        for (int i = 0; i < params; i++) {
          AsyncWebParameter* p = request->getParam(i);
          if (p->isPost()) {

            if (p->name() == "pass") Uncrypt_Pass = p->value().c_str();
            if (p->name() == "ip") SPIFFS_Config_Reseau["ap"][0]["ip"] = p->value();
            if (p->name() == "actif") SPIFFS_Config_Reseau["ap"][0]["actif"] = p->value();
          }
        }
        if (SPIFFS_EnregistrementAP()) {
          TITRE = "Redémarrage de l'apparail.";
          MESSAGE = "";
          request->send_P(200, "text/html", Page_Message, RetourVariable);
          delay(1000);
          ESP.restart();
        } else {
          request->send(200, "text/plain", "Erreur d'écriture de la configuration...");
        }

      } else if (String(p->value().c_str()) == "wifi") {

        int params = request->params();
        for (int i = 0; i < params; i++) {
          AsyncWebParameter* p = request->getParam(i);
          if (p->isPost()) {
            if (p->name() == "ssid") SPIFFS_SSID = p->value().c_str();
            if (p->name() == "pass") SPIFFS_Pass = p->value().c_str();
            if (p->name() == "ip") SPIFFS_IP = p->value().c_str();
          }
        }
        String networkError;
        if (!miraReadNetworkForm(request,networkError)) {
          request->send(400,"text/plain",networkError);return;
        }
        if (SPIFFS_EnregistrementReseau()) {
          if (ModeAP == false) {
            request->send_P(200, "text/html", Page_Index, RetourVariable);

          } else {
            request->send_P(200, "text/html", Page_Enregistrement, RetourVariable);
            delay(1000);
            ESP.restart();
          }

        } else {
          request->send(200, "text/plain", "Erreur d'écriture de la configuration...");
        }
      }

    } else if (String(p->name().c_str()) == "update") {

      if (String(p->value().c_str()) == "wifi") {
        int params = request->params();
        for (int i = 0; i < params; i++) {
          AsyncWebParameter* p = request->getParam(i);
          if (p->isPost()) {
            SPIFFS_ID = IDNetwork;

            if (p->name() == "ssid") SPIFFS_SSID = p->value().c_str();
            if (p->name() == "pass") SPIFFS_Pass = p->value().c_str();
            if (p->name() == "ip") SPIFFS_IP = p->value().c_str();
          }
        }
        String networkError;
        if (!miraReadNetworkForm(request,networkError)) {
          request->send(400,"text/plain",networkError);return;
        }
        if (SPIFFS_UpdateReseau()) {
          request->send_P(200, "text/html", Page_ReseauxEnreg, RetourVariable);
        } else {
          TITRE = "Erreur!";
          MESSAGE = "Erreur d'écriture de la configuration...";
          request->send_P(200, "text/html", Page_Message, RetourVariable);
        }
      }
    }
  });




  server.on("/updatemodule", HTTP_GET, [](AsyncWebServerRequest* request) {
    char CPass[50];
    Uncrypt_Pass.toCharArray(CPass, 50);
    if (!request->authenticate(Ident_ID, CPass)) return request->requestAuthentication();

    if (Action != ACTION_UPDATE) Action = ACTION_UPDATE;
    //server.end();
    //bool Result = update_module(UpdateURL);
    TITRE = "Mise à jour en cours...";
    MESSAGE = "Initialisation de la mise à jour.";
    request->send_P(200, "text/html", Page_Message, RetourVariable);
  });

  /*server.on("/updatemodule", HTTP_GET, [](AsyncWebServerRequest* request) {
    char* html = "<form method='POST' action='/doUpdate' enctype='multipart/form-data'><input type='file' name='update' accept='.bin'><input type='submit' value='Update'></form>";
    request->send(200, "text/html", html);
  });*/

  server.on(
    "/doUpdateFile", HTTP_POST,
    [](AsyncWebServerRequest* request) {
      if (miraManualUpdateSucceeded && !miraManualUpdateFailed) {
        request->send(200, "text/plain", "OK");
        if (_Debug) Serial.println("[MIRA][OTA] Reponse Web envoyee, redemarrage");
        delay(1000);
        Serial.flush();
        ESP.restart();
      } else {
        request->send(500, "text/plain", "UPDATE_FAILED");
      }
    },
    [](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
      handleDoUpdate(request, filename, index, data, len, final);
    });





  server.on("/api/checkupdate", HTTP_GET, [](AsyncWebServerRequest* request) {
    char CPass[50];
    Uncrypt_Pass.toCharArray(CPass, 50);
    if (!request->authenticate(Ident_ID, CPass)) return request->requestAuthentication();

    DynamicJsonDocument result(1024);
    String currentVersion = "";
    if (SetupModuleDone) {
      currentVersion = SPIFFS_Uncrypt(SPIFFS_Config_Module["module"][0]["version"].as<String>());
    }
    result["current"] = currentVersion;

    if (!SetupModuleDone || !currentVersion.length()) {
      result["status"] = "error";
      result["message"] = "Configuration du module indisponible.";
    } else {
      String githubPayload;
      int response = -1;
      String connectionError;

      for (uint8_t attempt = 1; attempt <= 3 && !githubPayload.length(); ++attempt) {
        WiFiClientSecure secure;
        secure.setInsecure();
        secure.setTimeout(15000);

        HTTPClient http;
        http.setTimeout(15000);
        http.setReuse(false);
        http.useHTTP10(true);
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        if (_Debug) {
          Serial.printf("[MIRA][OTA] Verification manuelle GitHub %u/3 - WiFi:%d RSSI:%d Heap:%u\n",
                        attempt, WiFi.status(), WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
        }

        if (!http.begin(secure, MIRA_GITHUB_VERSION_URL)) {
          connectionError = "initialisation HTTPS impossible";
          response = -1;
        } else {
          response = http.GET();
          if (response == HTTP_CODE_OK) {
            githubPayload = http.getString();
            http.end();
            break;
          }

          connectionError = (response < 0)
                              ? HTTPClient::errorToString(response)
                              : ("HTTP " + String(response));
          if (_Debug) {
            Serial.printf("[MIRA][OTA] Echec verification %u/3 : %d (%s)\n",
                          attempt, response, connectionError.c_str());
          }
          http.end();
        }

        if (attempt < 3) delay(500 * attempt);
      }

      if (response != HTTP_CODE_OK || !githubPayload.length()) {
        result["status"] = "error";
        result["message"] = "Impossible de contacter GitHub.";
        if (connectionError.length()) result["detail"] = connectionError + " (code " + String(response) + ")";
      } else {
        DynamicJsonDocument versionDoc(4096);
        DeserializationError err = deserializeJson(versionDoc, githubPayload);
        if (err) {
          result["status"] = "error";
          result["message"] = "Le fichier version.json de GitHub est invalide.";
        } else {
          String project = versionDoc["project"] | "";
          String remoteVersion = versionDoc["version"] | "";
          String binUrl = versionDoc["targets"][MIRA_DISPLAY_PROFILE_ID]["ota"] | "";

          if (project != "Mira Panel" || !remoteVersion.length() || !binUrl.startsWith("https://raw.githubusercontent.com/Arokh76/Mira-panel/firmware-beta/firmware/releases/")) {
            result["status"] = "error";
            result["message"] = "Les informations de mise à jour ne correspondent pas à Mira Panel.";
          } else {
            result["remote"] = remoteVersion;
            if (miraCompareVersion(remoteVersion, currentVersion) <= 0) {
              result["status"] = "uptodate";
              result["message"] = "Mira Panel est à jour.";
            } else {
              UpdateURL = binUrl;
              result["status"] = "available";
              result["message"] = "Une nouvelle version est disponible.";
            }
          }
        }
      }
    }

    String body;
    serializeJson(result, body);
    AsyncWebServerResponse* jsonResponse = request->beginResponse(200, "application/json", body);
    jsonResponse->addHeader("Cache-Control", "no-store, no-cache, must-revalidate");
    request->send(jsonResponse);
  });


  server.on("/load", HTTP_GET, [](AsyncWebServerRequest* request) {
    char CPass[50];
    Uncrypt_Pass.toCharArray(CPass, 50);
    if (!request->authenticate(Ident_ID, CPass))
      return request->requestAuthentication();
    String page = request->hasParam("nav") ? request->getParam("nav")->value() : "?";
    miraObserveNavigation(page.c_str());
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "nav") {

      if (String(p->value().c_str()) == "wifi") {
        request->send_P(200, "text/html", Page_ManageurWiFi, RetourVariable);
      } else if (String(p->value().c_str()) == "wifisr") {
        request->send_P(200, "text/html", Page_ManageurWiFiSansR, RetourVariable);
      } else if (String(p->value().c_str()) == "params") {
        request->send_P(200, "text/html", Page_Parametres, RetourVariable);
      } else if (String(p->value().c_str()) == "mqtt") {
        request->send_P(200, "text/html", Page_MQTT, RetourVariable);
      } else if (String(p->value().c_str()) == "system") {
        request->send_P(200, "text/html", Page_Sys, RetourVariable);
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
      } else if (String(p->value().c_str()) == "gpio") {
        request->send_P(200, "text/html", Page_GPIO, RetourVariable);
#endif
      } else if (String(p->value().c_str()) == "configAP") {
        request->send_P(200, "text/html", Page_ConfigAP, RetourVariable);
      } else if (String(p->value().c_str()) == "rst") {
        request->send_P(200, "text/html", Page_Donnees, RetourVariable);
      } else if (String(p->value().c_str()) == "listnetwork") {
        request->send_P(200, "text/html", Page_ReseauxEnreg, RetourVariable);
      } else if (String(p->value().c_str()) == "checkupdate") {
        String Retour;
        if (SetupModuleDone) {
          String version = MiraRuntimeVersion;
          if (!version.length()) {
            String storedVersion = SPIFFS_Config_Module["module"][0]["version"].as<String>();
            if (storedVersion.length()) version = SPIFFS_Uncrypt(storedVersion);
          }

          String githubPayload;
          int response = -1;
          String connectionError;

          for (uint8_t attempt = 1; attempt <= 3; ++attempt) {
            WiFiClientSecure secure;
            secure.setInsecure();
            secure.setTimeout(15000);

            HTTPClient http;
            http.setTimeout(15000);
            http.setReuse(false);
            http.useHTTP10(true);
            http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

            if (_Debug) {
              Serial.printf("[MIRA][OTA] Verification GitHub %u/3 - WiFi:%d RSSI:%d Heap:%u\n",
                            attempt, WiFi.status(), WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
            }

            if (!http.begin(secure, MIRA_GITHUB_VERSION_URL)) {
              connectionError = "initialisation HTTPS impossible";
              response = -1;
            } else {
              response = http.GET();
              if (response == HTTP_CODE_OK) {
                githubPayload = http.getString();
                http.end();
                break;
              }

              connectionError = (response < 0)
                                  ? HTTPClient::errorToString(response)
                                  : ("HTTP " + String(response));
              if (_Debug) {
                Serial.printf("[MIRA][OTA] Echec tentative %u : %d (%s)\n",
                              attempt, response, connectionError.c_str());
              }
              http.end();
            }

            if (attempt < 3) delay(500 * attempt);
          }

          if (response != HTTP_CODE_OK || !githubPayload.length()) {
            TITRE = "Mise à jour";
            Retour = "Impossible de vérifier les mises à jour sur GitHub après 3 tentatives.";
            if (connectionError.length()) {
              Retour += "<br><br><small>Détail : " + connectionError + " (code " + String(response) + ")</small>";
            }
          } else {
            DynamicJsonDocument doc(4096);
            DeserializationError err = deserializeJson(doc, githubPayload);
            if (err) {
              TITRE = "Mise à jour";
              Retour = "Le fichier version.json de GitHub est invalide.";
            } else {
              String project = doc["project"] | "";
              String remoteVersion = doc["version"] | "";
              String binUrl = doc["targets"][MIRA_DISPLAY_PROFILE_ID]["ota"] | "";
              if (project != "Mira Panel" || !remoteVersion.length() || !binUrl.startsWith("https://raw.githubusercontent.com/Arokh76/Mira-panel/firmware-beta/firmware/releases/")) {
                TITRE = "Mise à jour";
                Retour = "Les informations de mise à jour GitHub ne correspondent pas à Mira Panel.";
              } else if (miraCompareVersion(remoteVersion, version) <= 0) {
                TITRE = "Tout est OK !";
                Retour = "Mira Panel est à jour.<br><br>Version actuelle : <b>" + version + "</b>";
              } else {
                UpdateURL = binUrl;
                TITRE = "Nouvelle version disponible";
                Retour = "<b>Version actuelle :</b> " + version + "<br><br>";
                Retour += "<b>Nouvelle version :</b> " + remoteVersion + "<br><br>";
                Retour += "<form method=\"GET\" action=\"/updatemodule\">";
                Retour += "<input class=\"Bouton\" type=\"submit\" name=\"upload\" value=\"Installer la mise à jour\">";
                Retour += "</form>";
              }
            }
          }

          Retour += "<br><br><b><div>Mise à jour manuelle :</div></b>";
          Retour += "<form method='POST' action='/doUpdateFile' enctype='multipart/form-data'><input type='file' name='update' accept='.bin'><br><input class=\"Bouton\" type='submit' value='Installer le fichier .bin'></form>";
        } else {
          TITRE = "Mise à jour";
          Retour = "Configuration du module indisponible.<br><br>";
          Retour += "<form method='POST' action='/doUpdateFile' enctype='multipart/form-data'><input type='file' name='update' accept='.bin'><br><input class=\"Bouton\" type='submit' value='Installer le fichier .bin'></form>";
        }

        MESSAGE = Retour;
        request->send_P(200, "text/html", Page_Message, RetourVariable);

      } else if (String(p->value().c_str()) == "update") {
        p = request->getParam(1);
        if (String(p->name().c_str()) == "network") {
          IDNetwork = String(p->value().c_str());
          request->send_P(200, "text/html", Page_ModificationReseau, RetourVariable);
        }
      } else if (String(p->value().c_str()) == "id") {

        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        String Retour = "";

        for (int i = 0; i < Actions.size(); i++) {

          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "button") {
            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
            String Pass = Uncrypt_Pass;

            Retour += "<div class=\"FondCadre\">";
            Retour += "Nom: <b>" + Nom + "</b>";
            Retour += "<br>";
            Retour += "Type: <b>Button</b>";
            Retour += "<br>";
            Retour += "ID: <b>" + ID;
            Retour += "</b><br>";
            Retour += "<br>";
            Retour += "<a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&button=" + ID + "\">http://" + StringAdresseIP + "/update?key=" + Pass + "&button=" + ID + "</a>";
            Retour += "<br>";
            Retour += "</div>";
            Retour += "<br>";


          } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "httpaction") {
            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
            String Pass = Uncrypt_Pass;

            Retour += "<div class=\"FondCadre\">";
            Retour += "Nom: <b>" + Nom + "</b>";
            Retour += "<br>";
            Retour += "Type: <b>HTTP Action</b>";
            Retour += "<br>";
            Retour += "ID: <b>" + ID;
            Retour += "</b><br>";
            Retour += "<br>";
            Retour += "<a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&httpaction=" + ID + "\">http://" + StringAdresseIP + "/update?key=" + Pass + "&httpaction=" + ID + "</a>";
            Retour += "<br>";
            Retour += "</div>";
            Retour += "<br>";


          } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "toggle") {
            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String Value0 = SPIFFS_Config_Actions["actions"][i]["value0"].as<String>();
            String Value1 = SPIFFS_Config_Actions["actions"][i]["value1"].as<String>();
            String ID0 = SPIFFS_Config_Actions["actions"][i]["id0"].as<String>();
            String ID1 = SPIFFS_Config_Actions["actions"][i]["id1"].as<String>();
            String Pass = Uncrypt_Pass;

            Retour += "<div class=\"FondCadre\">";
            Retour += "Nom: <b>" + Nom + "</b>";
            Retour += "<br>";
            Retour += "Type: <b>Toggle</b>";
            Retour += "<br>";
            Retour += "ID - " + Value0 + ": <b>" + ID0;
            Retour += "</b><br>";
            Retour += "ID - " + Value1 + ": <b>" + ID1;
            Retour += "</b><br>";
            Retour += "<br>POST ";
            Retour += Value0 + ": <a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&toggle=" + ID0 + "\">http://" + StringAdresseIP + "/update?key=" + Pass + "&toggle=" + ID0 + "</a>";
            Retour += "<br>POST ";
            Retour += Value1 + ": <a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&toggle=" + ID1 + "\">http://" + StringAdresseIP + "/update?key=" + Pass + "&toggle=" + ID1 + "</a>";
            Retour += "<br>";
            Retour += "GET: <a href=\"http://" + StringAdresseIP + "/get?key=" + Pass + "&toggle=" + ID0 + "\">http://" + StringAdresseIP + "/get?key=" + Pass + "&toggle=" + ID0 + "</a>";
            Retour += "<br>";
            Retour += "</div>";
            Retour += "<br>";

          } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "info") {
            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
            String Pass = Uncrypt_Pass;

            Retour += "<div class=\"FondCadre\">";
            Retour += "Nom: <b>" + Nom + "</b>";
            Retour += "<br>";
            Retour += "Type: <b>Info</b>";
            Retour += "<br>";
            Retour += "ID: <b>" + ID;
            Retour += "</b><br>";
            Retour += "<br>POST: ";
            Retour += "<a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&info=" + ID + "&value=Info Test\">http://" + StringAdresseIP + "/update?key=" + Pass + "&info=" + ID + "&value=Info Test</a>";
            Retour += "<br>";
            Retour += "GET: <a href=\"http://" + StringAdresseIP + "/get?key=" + Pass + "&info=" + ID + "\">http://" + StringAdresseIP + "/get?key=" + Pass + "&info=" + ID + "</a>";
            Retour += "<br>";
            Retour += "</div>";
            Retour += "<br>";

          } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "inputbox") {
            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
            String Pass = Uncrypt_Pass;

            Retour += "<div class=\"FondCadre\">";
            Retour += "Nom: <b>" + Nom + "</b>";
            Retour += "<br>";
            Retour += "Type: <b>Input Box</b>";
            Retour += "<br>";
            Retour += "ID: <b>" + ID;
            Retour += "</b><br>";
            Retour += "<br>POST: ";
            Retour += "<a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&inputbox=" + ID + "&value=TextBox Test\">http://" + StringAdresseIP + "/update?key=" + Pass + "&inputbox=" + ID + "&value=TextBox Test</a>";
            Retour += "<br>";
            Retour += "</div>";
            Retour += "<br>";

          } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "infofa") {
            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
            String Pass = Uncrypt_Pass;

            Retour += "<div class=\"FondCadre\">";
            Retour += "Nom: <b>" + Nom + "</b>";
            Retour += "<br>";
            Retour += "Type: <b>Info FA</b>";
            Retour += "<br>";
            Retour += "ID: <b>" + ID;
            Retour += "</b><br>";
            Retour += "<br>POST: ";
            Retour += "<a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&info=" + ID + "&value=fa-solid fa-thumbs-up fa-5x\">http://" + StringAdresseIP + "/update?key=" + Pass + "&infofa=" + ID + "&value=fa-solid fa-thumbs-up fa-5x</a>";
            Retour += "<br>";
            Retour += "GET: <a href=\"http://" + StringAdresseIP + "/get?key=" + Pass + "&info=" + ID + "\">http://" + StringAdresseIP + "/get?key=" + Pass + "&infofa=" + ID + "</a>";
            Retour += "<br>";
            Retour += "</div>";
            Retour += "<br>";

          } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "color") {
            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
            String Pass = Uncrypt_Pass;

            Retour += "<div class=\"FondCadre\">";
            Retour += "Nom: <b>" + Nom + "</b>";
            Retour += "<br>";
            Retour += "Type: <b>Color</b>";
            Retour += "<br>";
            Retour += "ID: <b>" + ID;
            Retour += "</b><br>";
            Retour += "<br>POST: ";
            Retour += "<a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&color=" + ID + "&r=255&g=0&b=0\">http://" + StringAdresseIP + "/update?key=" + Pass + "&color=" + ID + "&r=255&g=0&b=0</a>";
            Retour += "<br>";
            Retour += "</div>";
            Retour += "<br>";
          } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "range") {
            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
            String Pass = Uncrypt_Pass;

            Retour += "<div class=\"FondCadre\">";
            Retour += "Nom: <b>" + Nom + "</b>";
            Retour += "<br>";
            Retour += "Type: <b>Range</b>";
            Retour += "<br>";
            Retour += "ID: <b>" + ID;
            Retour += "</b><br>";
            Retour += "<br>POST: ";
            Retour += "<a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&range=" + ID + "&value=75\">http://" + StringAdresseIP + "/update?key=" + Pass + "&range=" + ID + "&value=75</a>";
            Retour += "<br>";
            Retour += "</div>";
            Retour += "<br>";
          } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "list") {
            String NomListe = SPIFFS_Config_Actions["actions"][i]["nomliste"].as<String>();
            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
            String Pass = Uncrypt_Pass;

            Retour += "<div class=\"FondCadre\">";
            Retour += "Liste: <b>" + NomListe + "</b>";
            Retour += "<br>";
            Retour += "Nom: <b>" + Nom + "</b>";
            Retour += "<br>";
            Retour += "Type: <b>List</b>";
            Retour += "<br>";
            Retour += "ID: <b>" + ID;
            Retour += "</b><br>";
            Retour += "<br>POST: ";
            Retour += "<a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&list=" + NomListe + "&value=" + ID + "\">http://" + StringAdresseIP + "/update?key=" + Pass + "&list=" + NomListe + "&value=" + ID + "</a>";
            Retour += "<br>POST: ";
            Retour += "<a href=\"http://" + StringAdresseIP + "/update?key=" + Pass + "&listvalue=" + NomListe + "&value=" + Nom + "\">http://" + StringAdresseIP + "/update?key=" + Pass + "&listvalue=" + NomListe + "&value=" + Nom + "</a>";
            Retour += "<br>";
            Retour += "</div>";
            Retour += "<br>";
          }
        }
        TITRE = "Requêtes HTTP";
        MESSAGE = Retour;
        request->send_P(200, "text/html", Page_Message, RetourVariable);
      } else if (String(p->value().c_str()) == "removenetwork") {

        SPIFFS_ID = IDNetwork;
        if (SPIFFS_RemoveReseau()) {
          request->send_P(200, "text/html", Page_ReseauxEnreg, RetourVariable);
        } else {
          TITRE = "Erreur!";
          MESSAGE = "Impossible de supprimer ce réseau...";
          request->send_P(200, "text/html", Page_Message, RetourVariable);
        }
      } else if (String(p->value().c_str()) == "reboot") {
        request->redirect("/");
        unsigned long dly = millis();
        while (millis() - dly < 2000) {
        }
        ESP.restart();

      } else if (String(p->value().c_str()) == "rstnetwork") {
        JsonArray Reseau = SPIFFS_Config_Reseau["reseau"];
        if (Reseau.size() == 0) {
          if (_Debug == true) Serial.println(F("[MIRA] Configuration vide! Suppression inutile!"));
          request->send_P(200, "text/html", Page_Sys, RetourVariable);
        } else {
          SPIFFS_ResetReseau();
          SPIFFS_CreationConfigReseau();
          TITRE = "Paramètres réseaux supprimés!";
          MESSAGE = "";
          request->send_P(200, "text/html", Page_Message, RetourVariable);
        }
      } else if (String(p->value().c_str()) == "rstparams") {
        JsonArray Params = SPIFFS_Config_Params["params"];
        JsonArray lvgl = mira_Json_LVGL["lvgl"];
        if (Params.size() == 0 && lvgl.size()) {
          if (_Debug == true) Serial.println(F("[MIRA] Configuration vide! Suppression inutile!"));
          request->redirect("/load?nav=rst");
        } else {
          SPIFFS_ResetParams();
          SPIFFS_CreationConfigParams();
          SPIFFS_Creation_lvgl();
          TITRE = "Paramètres de configuration supprimés!";
          MESSAGE = "";

          request->redirect("/");
        }
      } else if (String(p->value().c_str()) == "rstall") {
        delay(100);
        SPIFFS_Format();
        delay(100);

        request->redirect("/");
        unsigned long dly = millis();
        while (millis() - dly < 1000) {
        }
        ESP.restart();
      }
    }
  });


  server.on("/get", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      AsyncWebParameter* p = request->getParam(1);

      if (String(p->name().c_str()) == "toogle" || String(p->name().c_str()) == "toggle") {

        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() == String(p->value().c_str()) || SPIFFS_Config_Actions["actions"][i]["id1"].as<String>() == String(p->value().c_str())) {

            String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            String State = SPIFFS_Config_Actions["actions"][i]["state"].as<String>();
            State.replace(Nom, "");
            request->send(200, "text/plain", State);

            break;
          }
          if (i == Actions.size() - 1) {
            request->send(200, "text/plain", "Null");
            break;
          }
        }
      } else if (String(p->name().c_str()) == "info") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {

            request->send(200, "text/plain", SPIFFS_Config_Actions["actions"][i]["value"].as<String>());

            break;
          }
          if (i == Actions.size() - 1) {
            request->send(200, "text/plain", "Null");
            break;
          }
        }

      } else if (String(p->name().c_str()) == "infofa") {
        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        for (int i = 0; i < Actions.size(); i++) {
          if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(p->value().c_str())) {

            request->send(200, "text/plain", SPIFFS_Config_Actions["actions"][i]["value"].as<String>());

            break;
          }
          if (i == Actions.size() - 1) {
            request->send(200, "text/plain", "Null");
            break;
          }
        }

      } else if (String(p->name().c_str()) == "id") {

        JsonArray Actions = SPIFFS_Config_Actions["actions"];
        String Retour = String(NomProg) + ": Identite des commandes.";
        Retour += "\n";
        Retour += "\n";
        for (int i = 0; i < Actions.size(); i++) {

          if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "button" || SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "info") {
            Retour += "Nom: " + SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            Retour += "\n";
            Retour += "ID: " + SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
            Retour += "\n";
            Retour += "\n";
          } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "toggle") {
            Retour += "Nom: " + SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
            Retour += "\n";
            Retour += "ID " + SPIFFS_Config_Actions["actions"][i]["value0"].as<String>() + ": " + SPIFFS_Config_Actions["actions"][i]["id0"].as<String>();
            Retour += "\n";
            Retour += "ID " + SPIFFS_Config_Actions["actions"][i]["value1"].as<String>() + ": " + SPIFFS_Config_Actions["actions"][i]["id1"].as<String>();
            Retour += "\n";
            Retour += "\n";
          }
        }




        request->send(200, "text/plain", Retour);
      } else if (String(p->name().c_str()) == "param") {

        String Parametre = String(p->value().c_str());
        JsonArray Params = SPIFFS_Config_Params["params"];
        for (int i = 0; i < Params.size(); i++) {
          if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == Parametre) {
            request->send(200, "text/html", SPIFFS_Config_Params["params"][i]["value"].as<String>());
            break;
          }
        }
      }
    }
  });

  server.on("/getallparams", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {

      String Retour_Json = SPIFFS_ReturnAllParams();
      request->send(200, "text/plain", Retour_Json);
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });




  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      if (_Debug == true) Serial.println(F("[MIRA] Redémarrage du module."));
      request->send(200, "text/plain", "OK");
      ESP.restart();
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });



  server.on("/updateparam", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      AsyncWebParameter* p = request->getParam(1);
      if (String(p->name().c_str()) == "param") {
        SPIFFS_ADD_Param = String(p->value().c_str());
        AsyncWebParameter* p = request->getParam(2);
        if (String(p->name().c_str()) == "value") {
          SPIFFS_ADD_Value = String(p->value().c_str());
          SPIFFS_UpdateParam();
          request->send(200, "text/plain", "OK");

        } else {
          request->send(200, "text/plain", "Paramètre invalide");
        }
      } else {
        request->send(200, "text/plain", "Paramètre invalide");
      }
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/addparam", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      AsyncWebParameter* p = request->getParam(1);
      if (String(p->name().c_str()) == "param") {
        SPIFFS_ADD_Param = String(p->value().c_str());
        AsyncWebParameter* p = request->getParam(2);
        if (String(p->name().c_str()) == "value") {
          SPIFFS_ADD_Value = String(p->value().c_str());
          AsyncWebParameter* p = request->getParam(3);
          if (String(p->name().c_str()) == "type") {
            if (String(p->value().c_str()) == "text" || String(p->value().c_str()) == "hidden-text" || String(p->value().c_str()) == "toogle" || String(p->value().c_str()) == "toggle") {
              SPIFFS_ADD_Type = String(p->value().c_str());
              SPIFFS_AddParam();

              request->send(200, "text/plain", "OK");

            } else {
              request->send(200, "text/plain", "Type invalide");
            }

          } else {
            request->send(200, "text/plain", "Paramètre invalide");
          }

        } else {
          request->send(200, "text/plain", "Paramètre invalide");
        }
      } else {
        request->send(200, "text/plain", "Paramètre invalide");
      }
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/addlistparam", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      AsyncWebParameter* p = request->getParam(1);
      if (String(p->name().c_str()) == "param") {
        SPIFFS_ADD_Param = String(p->value().c_str());
        AsyncWebParameter* p = request->getParam(2);
        if (String(p->name().c_str()) == "value") {
          SPIFFS_ADD_Value = String(p->value().c_str());
          AsyncWebParameter* p = request->getParam(3);
          if (String(p->name().c_str()) == "type") {
            if (String(p->value().c_str()) == "text" || String(p->value().c_str()) == "hidden-text" || String(p->value().c_str()) == "toogle" || String(p->value().c_str()) == "toggle" || String(p->value().c_str()) == "crypted-text") {
              SPIFFS_ADD_Type = String(p->value().c_str());
              SPIFFS_AddListParam();

              request->send(200, "text/plain", "OK");

            } else {
              request->send(200, "text/plain", "Type invalide");
            }

          } else {
            request->send(200, "text/plain", "Paramètre invalide");
          }

        } else {
          request->send(200, "text/plain", "Paramètre invalide");
        }
      } else {
        request->send(200, "text/plain", "Paramètre invalide");
      }
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/removelistlvgl", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {

      RemoveListLVGL();
      request->send(200, "text/plain", "OK");

    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  

  server.on("/addlistlvgl", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {

      AsyncWebParameter* p = request->getParam(1);
      if (String(p->name().c_str()) == "type") {
        LVGL_Type = String(p->value().c_str());

        AsyncWebParameter* p = request->getParam(2);
        if (String(p->name().c_str()) == "name") {
          LVGL_Name = String(p->value().c_str());

          AsyncWebParameter* p = request->getParam(3);
          if (String(p->name().c_str()) == "loc") {
            LVGL_Emplacement = String(p->value().c_str());

            AsyncWebParameter* p = request->getParam(4);
            if (String(p->name().c_str()) == "min") {
              LVGL_Min = String(p->value().c_str());

              AsyncWebParameter* p = request->getParam(5);
              if (String(p->name().c_str()) == "max") {
                LVGL_Max = String(p->value().c_str());

                AsyncWebParameter* p = request->getParam(6);
                if (String(p->name().c_str()) == "value") {
                  LVGL_Value = String(p->value().c_str());

                  AsyncWebParameter* p = request->getParam(7);
                  if (String(p->name().c_str()) == "unit") {
                    LVGL_Unit = String(p->value().c_str());
                    LVGL_Style = request->hasParam("style") ? request->getParam("style")->value() : "standard";
                    LVGL_Style.trim();
                    LVGL_Style.toLowerCase();
                    if (!LVGL_Style.length()) LVGL_Style = "standard";
                    LVGL_LabelMode = request->hasParam("labelMode") ? request->getParam("labelMode")->value() : "auto";
                    LVGL_LabelMode.toLowerCase();
                    if (LVGL_LabelMode != "text" && LVGL_LabelMode != "icon" &&
                        LVGL_LabelMode != "both") LVGL_LabelMode = "auto";
                    LVGL_Icon = request->hasParam("icon") ? request->getParam("icon")->value() : "auto";
                    LVGL_Icon.toLowerCase();
                    if (LVGL_Icon.length() > 24) LVGL_Icon = "auto";
                    LVGL_DisplayName = request->hasParam("displayName") ?
                        request->getParam("displayName")->value() : "";
                    if (LVGL_DisplayName.length() > 96) LVGL_DisplayName = LVGL_DisplayName.substring(0, 96);
                    LVGL_Layout = request->hasParam("layout") ? request->getParam("layout")->value() : "auto";
                    LVGL_Layout.trim();
                    LVGL_Layout.toLowerCase();
                    if (LVGL_Layout != "tile" && LVGL_Layout != "wide" && LVGL_Layout != "hero" && LVGL_Layout != "free") LVGL_Layout = "auto";

                    LVGL_LayoutX = request->hasParam("lx") ? request->getParam("lx")->value().toInt() : -1;
                    LVGL_LayoutY = request->hasParam("ly") ? request->getParam("ly")->value().toInt() : -1;
                    LVGL_LayoutW = request->hasParam("lw") ? request->getParam("lw")->value().toInt() : -1;
                    LVGL_LayoutH = request->hasParam("lh") ? request->getParam("lh")->value().toInt() : -1;
                    if (LVGL_Layout == "free") {
                      if (LVGL_LayoutX < 0 || LVGL_LayoutY < 0 || LVGL_LayoutW <= 0 || LVGL_LayoutH <= 0) {
                        LVGL_Layout = "auto";
                      }
                    } else {
                      LVGL_LayoutX = LVGL_LayoutY = LVGL_LayoutW = LVGL_LayoutH = -1;
                    }
                    AddListLVGL();
                    request->send(200, "text/plain", "OK");

                  } else {
                    request->send(200, "text/plain", "Paramètre invalide6");
                  }
                } else {
                  request->send(200, "text/plain", "Paramètre invalide5");
                }
              } else {
                request->send(200, "text/plain", "Paramètre invalide4");
              }
            } else {
              request->send(200, "text/plain", "Paramètre invalide3");
            }
          } else {
            request->send(200, "text/plain", "Paramètre invalide2");
          }
        } else {
          request->send(200, "text/plain", "Paramètre invalide1");
        }
      } else {
        request->send(200, "text/plain", "Paramètre invalide0");
      }
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/screenadd", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {

      AsyncWebParameter* p = request->getParam(1);
      if (String(p->name().c_str()) == "type") {
        LVGL_Type = String(p->value().c_str());

        AsyncWebParameter* p = request->getParam(2);
        if (String(p->name().c_str()) == "name") {
          LVGL_Name = String(p->value().c_str());

          AsyncWebParameter* p = request->getParam(3);
          if (String(p->name().c_str()) == "loc") {
            LVGL_Emplacement = String(p->value().c_str());

            AsyncWebParameter* p = request->getParam(4);
            if (String(p->name().c_str()) == "min") {
              LVGL_Min = String(p->value().c_str());

              AsyncWebParameter* p = request->getParam(5);
              if (String(p->name().c_str()) == "max") {
                LVGL_Max = String(p->value().c_str());

                AsyncWebParameter* p = request->getParam(6);
                if (String(p->name().c_str()) == "value") {
                  LVGL_Value = String(p->value().c_str());

                  AsyncWebParameter* p = request->getParam(7);
                  if (String(p->name().c_str()) == "unit") {
                    LVGL_Unit = String(p->value().c_str());
                    LVGL_Style = request->hasParam("style") ? request->getParam("style")->value() : "standard";
                    LVGL_Style.trim();
                    LVGL_Style.toLowerCase();
                    if (!LVGL_Style.length()) LVGL_Style = "standard";
                    LVGL_LabelMode = request->hasParam("labelMode") ? request->getParam("labelMode")->value() : "auto";
                    LVGL_LabelMode.toLowerCase();
                    if (LVGL_LabelMode != "text" && LVGL_LabelMode != "icon" &&
                        LVGL_LabelMode != "both") LVGL_LabelMode = "auto";
                    LVGL_Icon = request->hasParam("icon") ? request->getParam("icon")->value() : "auto";
                    LVGL_Icon.toLowerCase();
                    if (LVGL_Icon.length() > 24) LVGL_Icon = "auto";
                    LVGL_DisplayName = request->hasParam("displayName") ?
                        request->getParam("displayName")->value() : "";
                    if (LVGL_DisplayName.length() > 96) LVGL_DisplayName = LVGL_DisplayName.substring(0, 96);
                    LVGL_Layout = request->hasParam("layout") ? request->getParam("layout")->value() : "auto";
                    LVGL_Layout.trim();
                    LVGL_Layout.toLowerCase();
                    if (LVGL_Layout != "tile" && LVGL_Layout != "wide" && LVGL_Layout != "hero" && LVGL_Layout != "free") LVGL_Layout = "auto";

                    LVGL_LayoutX = request->hasParam("lx") ? request->getParam("lx")->value().toInt() : -1;
                    LVGL_LayoutY = request->hasParam("ly") ? request->getParam("ly")->value().toInt() : -1;
                    LVGL_LayoutW = request->hasParam("lw") ? request->getParam("lw")->value().toInt() : -1;
                    LVGL_LayoutH = request->hasParam("lh") ? request->getParam("lh")->value().toInt() : -1;
                    if (LVGL_Layout == "free") {
                      if (LVGL_LayoutX < 0 || LVGL_LayoutY < 0 || LVGL_LayoutW <= 0 || LVGL_LayoutH <= 0) {
                        LVGL_Layout = "auto";
                      }
                    } else {
                      LVGL_LayoutX = LVGL_LayoutY = LVGL_LayoutW = LVGL_LayoutH = -1;
                    }
                    AddListLVGL();
                    String RetJson = "";
                    serializeJsonPretty(mira_Json_LVGL, RetJson);
                    request->send(200, "text/plain", RetJson);

                  } else {
                    request->send(200, "text/plain", "Paramètre invalide6");
                  }
                } else {
                  request->send(200, "text/plain", "Paramètre invalide5");
                }
              } else {
                request->send(200, "text/plain", "Paramètre invalide4");
              }
            } else {
              request->send(200, "text/plain", "Paramètre invalide3");
            }
          } else {
            request->send(200, "text/plain", "Paramètre invalide2");
          }
        } else {
          request->send(200, "text/plain", "Paramètre invalide1");
        }
      } else {
        request->send(200, "text/plain", "Paramètre invalide0");
      }
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/screenformat", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {

      RemoveListLVGL();
      request->send(200, "text/plain", "Liste vidée!");

    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });
  
  server.on("/screensave", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      SaveListLVGL();
      request->send(200, "text/plain", "Liste sauvegardée!");
      delay(500);
      ESP.restart();
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/savelistlvgl", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      SaveListLVGL();
      request->send(200, "text/plain", "OK");
      delay(500);
      ESP.restart();
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/createlistparam", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      SPIFFS_SaveParam_List();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/updatelistparam", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      AsyncWebParameter* p = request->getParam(1);
      if (String(p->name().c_str()) == "param") {
        SPIFFS_ADD_Param = String(p->value().c_str());
        AsyncWebParameter* p = request->getParam(2);
        if (String(p->name().c_str()) == "value") {
          SPIFFS_ADD_Value = String(p->value().c_str());

          SPIFFS_ADD_Type = String(p->value().c_str());
          SPIFFS_UpdateListParam();

          request->send(200, "text/plain", "OK");



        } else {
          request->send(200, "text/plain", "Paramètre invalide");
        }

      } else {
        request->send(200, "text/plain", "Paramètre invalide");
      }
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/savelistparam", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      SPIFFS_SaveParam_List();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/getparam", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    JsonArray Actions = SPIFFS_Config_Params["params"];
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      p = request->getParam(1);
      for (int i = 0; i < Actions.size(); i++) {
        if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == String(p->value().c_str())) {
          if (SPIFFS_Config_Params["params"][i]["type"].as<String>() == "crypted-text") {
            String Crypt = SPIFFS_Config_Params["params"][i]["value"].as<String>();
            String Decrypt = SPIFFS_Uncrypt(Crypt);

            request->send(200, "text/plain", String(Decrypt));
          } else {
            request->send(200, "text/plain", SPIFFS_Config_Params["params"][i]["value"].as<String>());
          }
          break;
        }
        if (i == Actions.size() - 1) {
          request->send(200, "text/plain", "Null");
          break;
        }
      }
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/getmodule", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {


      request->send(200, "text/plain", SPIFFS_Config_Module["module"][0]["nom"].as<String>());
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/testmodule", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {


      request->send(200, "text/plain", "Tout est OK");
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/setjeedomid", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      p = request->getParam(1);
      String id = String(p->value().c_str());
      p = request->getParam(2);
      String valuev = String(p->value().c_str());
      if (_Debug == true) Serial.print(F("[MIRA] Modification du Json de l'index: "));
      if (_Debug == true) Serial.print(F("id: "));
      if (_Debug == true) Serial.print(id);
      if (_Debug == true) Serial.print(F(" Value: "));
      if (_Debug == true) Serial.println(valuev);

      SetJeedomId(id, valuev);
      //initWiFi();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/sendjeedomorderid", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      p = request->getParam(1);
      String id = String(p->value().c_str());

      OrderIdJeedom(id);



      //initWiFi();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/setjeedomtoggleid", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      p = request->getParam(1);
      String id = String(p->value().c_str());
      p = request->getParam(2);
      String value0 = String(p->value().c_str());
      p = request->getParam(3);
      String value1 = String(p->value().c_str());
      if (_Debug == true) Serial.print(F("[MIRA] Modification du Json de l'index: "));
      if (_Debug == true) Serial.print(F("id: "));
      if (_Debug == true) Serial.print(id);
      if (_Debug == true) Serial.print(F(" Value 0: "));
      if (_Debug == true) Serial.print(value0);
      if (_Debug == true) Serial.print(F(" / Value 1: "));
      if (_Debug == true) Serial.println(value1);

      SetJeedomToggleId(id, value0, value1);
      //initWiFi();
      request->send(200, "text/plain", "OK");
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/getindex", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      String Retour_Json = ReturnJSONIndex();
      request->send(200, "text/plain", Retour_Json);
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });

  server.on("/programui", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("key", true) || request->getParam("key", true)->value() != Uncrypt_Pass) {
      request->send(200, "text/plain", "Clé invalide");
      return;
    }
    if (!request->hasParam("json", true)) {
      request->send(400, "text/plain", "Configuration absente");
      return;
    }

    String raw = request->getParam("json", true)->value();
    DynamicJsonDocument incoming(16384);
    DeserializationError err = deserializeJson(incoming, raw);
    if (err || !incoming["lvgl"].is<JsonArray>()) {
      request->send(400, "text/plain", "Configuration invalide");
      return;
    }

    mira_Json_LVGL.clear();
    mira_Json_LVGL.set(incoming);
    if (mira_Json_LVGL.overflowed()) {
      request->send(413, "text/plain", "Configuration trop grande");
      return;
    }

    // SaveListLVGL keeps the same runtime reload/event path as legacy programming.
    NB_List_lvgl = incoming["lvgl"].size();
    if (NB_List_lvgl == 0) NB_List_lvgl = 1;
    SaveListLVGL();
    request->send(200, "text/plain", "OK");
  });

  // Camera URLs are configured as normal Jeedom commands. Do not reprogram
  // /miralvgl.json or rebuild every widget when a camera URL changes.
  server.on("/camera/config", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("key", true) ||
        request->getParam("key", true)->value() != Uncrypt_Pass) {
      request->send(403, "text/plain", "Cle invalide"); return;
    }
    if (!request->hasParam("name", true) || !request->hasParam("url", true)) {
      request->send(400, "text/plain", "Nom ou URL manquante"); return;
    }
    const String name = request->getParam("name", true)->value();
    const String url = request->getParam("url", true)->value();
    if (name.length() < 1 || name.length() > 100 || !miraCameraSafeUrl(url)) {
      request->send(400, "text/plain", "URL camera invalide"); return;
    }
    bool found = false;
    JsonArray widgets = mira_Json_LVGL["lvgl"].as<JsonArray>();
    for (JsonObject widget : widgets) {
      if (widget["type"].as<String>() == "camera" &&
          widget["name"].as<String>() == name) {
        found = true;
        break;
      }
    }
    if (!found) {
      request->send(404, "text/plain", "Widget camera introuvable"); return;
    }
    Preferences prefs;
    if (!prefs.begin("miracam", false)) {
      request->send(503, "text/plain", "NVS indisponible"); return;
    }
    const String storageKey = miraCameraNvsKey(name);
    // Persist even a blank string: clearing the command disables the stream.
    const size_t written = prefs.putString(storageKey.c_str(), url);
    prefs.end();
    if (written != url.length()) {
      request->send(503, "text/plain", "Sauvegarde URL impossible"); return;
    }
    request->send(200, "text/plain", "OK");
  });

  // Individual page visibility is stored in the same miraui NVS namespace as
  // the persistent favorites. Screen reload is not required. Home is mandatory.
  server.on("/navpages", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("key", true) ||
        request->getParam("key", true)->value() != Uncrypt_Pass) {
      request->send(403, "text/plain", "Cle invalide"); return;
    }
    if (!request->hasParam("mask", true)) {
      request->send(400, "text/plain", "Masque absent"); return;
    }
    const String value = request->getParam("mask", true)->value();
    for (uint8_t i = 0; i < value.length(); ++i) {
      if (!isDigit(value[i])) {
        request->send(400, "text/plain", "Masque invalide"); return;
      }
    }
    const int mask = value.toInt();
    if (mask < 1 || mask > 63 || !(mask & 1)) {
      request->send(400, "text/plain", "Accueil obligatoire"); return;
    }
    Preferences prefs;
    if (!prefs.begin("miraui", false)) {
      request->send(503, "text/plain", "NVS indisponible"); return;
    }
    const bool stored = prefs.putUChar("pageMask", (uint8_t)mask) == 1;
    prefs.end();
    request->send(stored ? 200 : 503, "text/plain",
                  stored ? "OK" : "Ecriture masque echouee");
  });

  server.on("/navpages", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("key") ||
        request->getParam("key")->value() != Uncrypt_Pass) {
      request->send(403, "text/plain", "Cle invalide"); return;
    }
    Preferences prefs;
    if (!prefs.begin("miraui", true)) {
      request->send(503, "text/plain", "NVS indisponible"); return;
    }
    const uint8_t mask = prefs.getUChar("pageMask", 0x3F) | 1;
    prefs.end();
    DynamicJsonDocument response(128);
    response["mask"] = mask;
    String body; serializeJson(response, body);
    request->send(200, "application/json", body);
  });

  // Dedicated authenticated navigation endpoint; no /programui/reboot.
  server.on("/navfavorites", HTTP_POST, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("key", true) ||
        request->getParam("key", true)->value() != Uncrypt_Pass) {
      request->send(403, "text/plain", "Cle invalide"); return;
    }
    if (!request->hasParam("json", true)) {
      request->send(400, "text/plain", "Favoris absents"); return;
    }
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, request->getParam("json", true)->value()) ||
        !doc["favorites"].is<JsonArray>() || doc["favorites"].size() != 3) {
      request->send(400, "text/plain", "Format favoris invalide"); return;
    }
    uint8_t favorites[3];
    JsonArray input = doc["favorites"].as<JsonArray>();
    for (uint8_t i = 0; i < 3; ++i) {
      JsonVariant v = input[i];
      if (!v.is<int>() && !v.is<unsigned int>()) {
        request->send(400, "text/plain", "Favori non numerique"); return;
      }
      int page = v.as<int>();
      if (page != 255 && (page < 0 || page > 5)) {
        request->send(400, "text/plain", "Page favorite invalide"); return;
      }
      for (uint8_t j = 0; j < i; ++j) {
        if (page != 255 && page == favorites[j]) {
          request->send(400, "text/plain", "Favoris dupliques"); return;
        }
      }
      favorites[i] = (uint8_t)page;
    }
    Preferences prefs;
    if (!prefs.begin("miraui", false)) {
      request->send(503, "text/plain", "NVS indisponible"); return;
    }
    bool stored = true;
    for (uint8_t i = 0; i < 3; ++i) {
      char key[8]; snprintf(key, sizeof(key), "fav%u", i);
      if (prefs.putUChar(key, favorites[i]) != 1) stored = false;
    }
    prefs.end();
    request->send(stored ? 200 : 503, "text/plain",
                  stored ? "OK" : "Ecriture favoris echouee");
  });

  server.on("/navfavorites", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("key") ||
        request->getParam("key")->value() != Uncrypt_Pass) {
      request->send(403, "text/plain", "Cle invalide"); return;
    }
    Preferences prefs;
    if (!prefs.begin("miraui", true)) {
      request->send(503, "text/plain", "NVS indisponible"); return;
    }
    DynamicJsonDocument response(128);
    JsonArray favorites = response.createNestedArray("favorites");
    for (uint8_t i = 0; i < 3; ++i) {
      char key[8]; snprintf(key, sizeof(key), "fav%u", i);
      favorites.add(prefs.getUChar(key, i));
    }
    prefs.end();
    String body; serializeJson(response, body);
    request->send(200, "application/json", body);
  });

  server.on("/displayinfo", HTTP_GET, [](AsyncWebServerRequest* request) {
    if (!request->hasParam("key") || request->getParam("key")->value() != Uncrypt_Pass) {
      request->send(200, "text/plain", "Clé invalide");
      return;
    }

    DynamicJsonDocument displayInfo(384);
    displayInfo["profile"] = MIRA_DISPLAY_PROFILE_ID;
    displayInfo["width"] = MIRA_DISPLAY_WIDTH;
    displayInfo["height"] = MIRA_DISPLAY_HEIGHT;
    displayInfo["contentWidth"] = MIRA_DISPLAY_WIDTH;
    displayInfo["contentHeight"] = MIRA_CONTENT_HEIGHT;
    displayInfo["navHeight"] = MIRA_NAV_HEIGHT;
    displayInfo["rotation"] = MIRA_DISPLAY_ROTATION;
    displayInfo["logicalWidth"] = MIRA_LAYOUT_LOGICAL_WIDTH;
    displayInfo["logicalHeight"] = MIRA_LAYOUT_LOGICAL_HEIGHT;
    displayInfo["httpPrimary"] = true;
    displayInfo["mqttOptional"] = true;
    displayInfo["freeLayout"] = true;
    displayInfo["widgetLabelModes"] = "auto,text,icon,both";
    displayInfo["widgetIcons"] = "auto,temperature,humidity,pressure,light,energy,plug,door";

    String Retour_Json = "";
    serializeJson(displayInfo, Retour_Json);
    request->send(200, "application/json", Retour_Json);
  });

  server.on("/getlvgl", HTTP_GET, [](AsyncWebServerRequest* request) {
    AsyncWebParameter* p = request->getParam(0);
    if (String(p->name().c_str()) == "key" && String(p->value().c_str()) == Uncrypt_Pass) {
      String Retour_Json = ReturnJSONlvgl();
      request->send(200, "text/plain", Retour_Json);
    } else {
      request->send(200, "text/plain", "Clé invalide");
    }
  });


  events.onConnect([](AsyncEventSourceClient* client) {
    if (_Debug == true) Serial.println(F("[MIRA] Client SSE connecte."));
    if (client->lastId()) {
      if (_Debug == true) Serial.printf("[MIRA] Client SSE reconnecte. Dernier ID : %u\n", client->lastId());
    }
    client->send("[MIRA] Un salut de Mira!", NULL, millis(), 10000);
  });
  server.addHandler(&events);

  server.onNotFound([](AsyncWebServerRequest* request) {
    TITRE = "Erreur: 404";
    MESSAGE = "Oups! Cette page n'existe pas...";
    request->send_P(200, "text/html", Page_Message, RetourVariable);
  });

  server.begin();
}


MiraPanelServer::MiraPanelServer(char* NomProgramme) {
  NomProg = NomProgramme;
}

bool MiraPanelServer::ResetParams() {
  JsonArray Params = SPIFFS_Config_Params["params"];
  if (Params.size() == 0) {
    if (_Debug == true) Serial.println(F("[MIRA] Configuration vide! Suppression inutile!"));
    return false;
  } else {
    SPIFFS_ResetParams();
    SPIFFS_CreationConfigParams();
    SPIFFS_Creation_lvgl();
    return true;
  }
}

bool MiraPanelServer::ResetNetwork() {
  JsonArray Reseau = SPIFFS_Config_Reseau["reseau"];
  if (Reseau.size() == 0) {
    if (_Debug == true) Serial.println(F("[MIRA] Configuration vide! Suppression inutile!"));
    return false;
  } else {
    SPIFFS_ResetReseau();
    SPIFFS_CreationConfigReseau();
    return true;
  }
}
void MiraPanelServer::ResetAll() {
  SPIFFS_Format();
}

void MiraPanelServer::RSTHandler() {
  if (RST_Button) {

    if (digitalRead(Bouton_RST) == LOW) {

      if (!DoOneRST) {
        if (_Debug) Serial.println(F("[MIRA] Bouton reset appuye : reinitialisation dans 15 secondes"));
        RST_Moment_Memorise = millis();
        DoOneRST = true;
      }

      if (RST_Moment_Memorise != 0) {
        RST_Moment_Present = millis();
        if (RST_Moment_Present - RST_Moment_Memorise >= 10000 && RST_Moment_Present - RST_Moment_Memorise < 15000) {
          if (_Debug) Serial.println(F("[MIRA] Bouton reset appuye : reinitialisation dans 5 secondes"));

          if (Led_RST > 0) {
            digitalWrite(Led_RST, HIGH);
          }
        }
        if (RST_Moment_Present - RST_Moment_Memorise >= 15000) {
          if (Led_RST > 0) {
            digitalWrite(Led_RST, LOW);
          }
          if (_Debug) Serial.println(F("[MIRA] Reinitialisation complete du module"));
          delay(1000);
          if (Led_RST > 0) {
            digitalWrite(Led_RST, HIGH);
          }

          SPIFFS_Format(true);
          if (Led_RST > 0) {
            digitalWrite(Led_RST, LOW);
          }

          DoOneRST = false;
          RST_Moment_Memorise = 0;
          RST_Moment_Present = 0;
        }
      }

    } else {

      if (DoOneRST) {
        if (Led_RST > 0) {
          digitalWrite(Led_RST, LOW);
        }
        if (_Debug) Serial.println(F("[MIRA] Reinitialisation annulee."));
        DoOneRST = false;
        RST_Moment_Memorise = 0;
        RST_Moment_Present = 0;
      }
    }
  } else {
  }
}



int currentLength = 0;
int initLength = 0;
int prec = 0;
int memprec = 0;
bool MiraPanelServer::update_module(String fileName) {
  if (_Debug) Serial.println("[MIRA][OTA] URL: " + fileName);
  if (!fileName.length()) return false;

  HTTPClient http;
  WiFiClient plainClient;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  bool begun = fileName.startsWith("https://") ? http.begin(secureClient, fileName) : http.begin(plainClient, fileName);
  if (!begun) {
    if (_Debug) Serial.println("[MIRA][OTA] Impossible d'initialiser HTTP");
    return false;
  }
  http.setTimeout(15000);
  int resp = http.GET();
  if (_Debug) Serial.printf("[MIRA][OTA] HTTP %d\n", resp);
  if (resp != HTTP_CODE_OK) {
    http.end();
    return false;
  }

  int fileSizeSigned = http.getSize();
  if (fileSizeSigned <= 0) {
    if (_Debug) Serial.println("[MIRA][OTA] Taille du firmware invalide");
    http.end();
    return false;
  }
  size_t fileSize = (size_t)fileSizeSigned;
  int cmd = (fileName.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
  if (!Update.begin(fileSize, cmd)) {
    Update.printError(Serial);
    http.end();
    return false;
  }

  currentLength = 0;
  initLength = (int)fileSize;
  prec = 0;
  memprec = -1;

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buff[1024];
  size_t remaining = fileSize;
  while (http.connected() && remaining > 0) {
    size_t available = stream->available();
    if (available) {
      int c = stream->readBytes(buff, (available > sizeof(buff)) ? sizeof(buff) : available);
      if (c <= 0) break;
      size_t written = Update.write(buff, (size_t)c);
      if (written != (size_t)c) {
        Update.printError(Serial);
        Update.abort();
        http.end();
        return false;
      }
      currentLength += c;
      remaining -= (size_t)c;
      prec = (currentLength * 100) / initLength;
      if (prec != memprec) {
        memprec = prec;
        String progress = "Progression: " + String(memprec) + " %";
        events.send(progress.c_str(), "divmessage", millis());
        if (_Debug) Serial.println("[MIRA][OTA] " + String(memprec) + "%");
      }
    }
    delay(1);
  }

  bool ok = (remaining == 0) && Update.end(true);
  if (!ok) {
    Update.printError(Serial);
    http.end();
    return false;
  }

  events.send("Redémarrage de Mira Panel", "divmessage", millis());
  if (_Debug) Serial.println("[MIRA][OTA] Mise à jour réussie");
  http.end();
  delay(500);
  ESP.restart();
  return true;
}


bool MiraPanelServer::WT32HasAction(const String& name) {
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
  return MiraWT32GPIO::actionMapped(name);
#else
  (void)name; return false;
#endif
}
bool MiraPanelServer::WT32ExecuteAction(const String& name) {
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
  return MiraWT32GPIO::executeAction(name);
#else
  (void)name; return false;
#endif
}
bool MiraPanelServer::WT32HasToggle(const String& name) {
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
  return MiraWT32GPIO::anyToggleLink(name);
#else
  (void)name; return false;
#endif
}
bool MiraPanelServer::WT32ExecuteToggle(const String& name, bool on) {
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
  return MiraWT32GPIO::executeToggle(name, on);
#else
  (void)name; (void)on; return false;
#endif
}
bool MiraPanelServer::WT32ReadToggle(const String& name, bool& state) {
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
  return MiraWT32GPIO::toggleState(name, state);
#else
  (void)name; (void)state; return false;
#endif
}

void MiraPanelServer::WT32NotifyGpioChanges() {
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
  // A write may originate from the web server's network task. Only the main
  // loop sends HTTP. The payload requests a READ-ONLY state synchronization.
  static uint32_t sentRevision = 0;
  static uint32_t lastAttemptMs = 0;
  const uint32_t revision = MiraWT32GPIO::stateRevision;
  if (revision == sentRevision) return;
  const uint32_t now = millis();
  if (now - MiraWT32GPIO::changedAtMs < 150 || now - lastAttemptMs < 250) return;
  lastAttemptMs = now;
  if (WiFi.status() != WL_CONNECTED || !GetParamBool("Jeedom")) return;
  const String urlBase = GetParamText("Jeedom_IP");
  const String apiKey = GetParamText("Jeedom_Key_mira");
  const String refreshId = GetParamText("Jeedom_Refresh");
  if (!urlBase.startsWith("http") || apiKey.isEmpty() || refreshId.isEmpty() ||
      apiKey == "null" || refreshId == "null") return;
  const String url = urlBase +
      "/core/api/jeeApi.php?plugin=mira&type=event&apikey=" + apiKey +
      "&id=" + refreshId + "&gpio_sync=1";
  // Dedicated async worker: never steal / overwrite the general Jeedom
  // command request and its response acknowledgement.
  static AsyncHTTPRequest gpioFeedbackRequest;
  const int ready = gpioFeedbackRequest.readyState();
  if (ready != readyStateUnsent && ready != readyStateDone) return;
  if (!gpioFeedbackRequest.open("GET", url.c_str())) return;
  gpioFeedbackRequest.send();
  sentRevision = revision;
  // The minute-based Jeedom cron is the fallback if this HTTP send fails.
  if (_Debug) Serial.printf("[MIRA][GPIO] Notification Jeedom revision %u\n",
                            unsigned(revision));
#endif
}


bool MiraPanelServer::WT32MultiVisible(const String& name) {
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
  return MiraWT32GPIO::isMultiVisible(name);
#else
  (void)name; return false;
#endif
}

void MiraPanelServer::WT32PollMultiButtons() {
#if !defined(MIRA_TARGET_WAVESHARE_43C) && !defined(MIRA_TARGET_NEXTION_28)
  String name; uint8_t kind=0;
  if (!MiraWT32GPIO::multiPoll(name,kind)) return;
  const char* prefixes[]={"Multi_Clic_","Multi_Double_","Multi_Triple_","Multi_Long_"};
  if(kind>=4) return;
  const String logicalName=String(prefixes[kind])+name;
  JsonArray actions=SPIFFS_Config_Actions["actions"];
  for (JsonObject action : actions) {
    if(action["type"].as<String>()!="button" ||
       action["nom"].as<String>()!=logicalName) continue;
    const int id=action["id"] | -1;
    if(id>=0) {
      IndexSetEvent(id);
      if(_Debug) Serial.println("[MIRA][MULTI] "+logicalName);
    }
    return;
  }
#endif
}

void MiraPanelServer::IndexSetEvent(int ID) {
  Resend = true;
  Action = ID;
}

String MiraPanelServer::GetCameraUrl(const String& name) {
  Preferences prefs;
  if (!prefs.begin("miracam", true)) return "";
  const String key = miraCameraNvsKey(name);
  const String value = prefs.getString(key.c_str(), "");
  prefs.end();
  return value;
}

String MiraPanelServer::GetJsonLvgl() {
  String Retour_Json = "";
  serializeJsonPretty(mira_Json_LVGL, Retour_Json);
  return Retour_Json;
}

void MiraPanelServer::ResetActionsForLvgl() {
  // Les IndexAdd* sont construits dynamiquement a partir de /miralvgl.json.
  // Lors d'une reprogrammation depuis Jeedom il faut repartir de l'ID 1,
  // sinon les IDs s'empilent et /updatenr ne correspond plus aux commandes Mira Panel.
  SPIFFS_Config_Actions.clear();
  SPIFFS_Config_Actions.createNestedArray("actions");
  IDAction = 1;
  Action = -1;
  Resend = true;
  // Force un nouvel appel a la commande init_cmd Jeedom au prochain tour de loop
  // pour remettre en place les IDs Jeedom de la nouvelle table.
  Init_Jeedom = false;

  NB_Listes = 0;
  ID_Memoire_Liste = 0;
  for (int i = 0; i < 99; ++i) {
    Memoire_Liste[i] = "";
    Listes[i] = "";
    Listes_Value[i] = "";
  }

  if (_Debug) Serial.println(F("[MIRA] Table des actions LVGL remise a zero"));
}

String MiraPanelServer::HTTPGet(String URL) {
  if (URL != "") {
    HTTPClient http;
    http.begin(URL);
    http.addHeader("Content-Type", "text/plain");
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      String response = http.getString();
      return response;
    } else {
      if (_Debug == true) Serial.print(F("[MIRA] Erreur GET HTTP: "));
      if (_Debug == true) Serial.println(httpResponseCode);
      return "Error";
    }
    http.end();
    delay(10);
  }
}

void MiraPanelServer::HTTPPost(String URL) {
  if (URL != "") {
    HTTPClient http;
    http.begin(URL);
    http.addHeader("Content-Type", "text/plain");
    int httpResponseCode = http.POST("HTTP POST Mira Panel Server Manager");
    if (httpResponseCode > 0) {

    } else {
      if (_Debug == true) Serial.print(F("[MIRA] Erreur POST HTTP: "));
      if (_Debug == true) Serial.println(httpResponseCode);
    }
    http.end();
    delay(10);
  }
}


String MiraPanelServer::GetKey() {
  return Uncrypt_Pass;
}

String MiraPanelServer::GetIP() {
  return SPIFFS_IP;
}




void MiraPanelServer::SetupModule(String Module, String ID_Module, String Version, String MAJ, String Type) {
  MiraRuntimeVersion = Version;
  JsonArray TableMod = SPIFFS_Config_Module["module"];
  TableMod.clear();
  JsonObject RowMod = TableMod.createNestedObject();
  RowMod["nom"] = Module;
  RowMod["module"] = SPIFFS_Crypt(ID_Module);
  RowMod["version"] = SPIFFS_Crypt(Version);
  RowMod["url"] = SPIFFS_Crypt(MAJ);
  RowMod["type"] = SPIFFS_Crypt(Type);
  SetupModuleDone = true;
}



void MiraPanelServer::IndexAddSeg() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["nom"] = "eeSeg";
  Action["type"] = "eeSeg";
}

void MiraPanelServer::IndexAddSegWhite() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["nom"] = "eeSegw";
  Action["type"] = "eeSegw";
}

int MiraPanelServer::IndexAddButton(String Name, bool Return_Line) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "button";
  if (Return_Line) Action["return"] = "true";
  else Action["return"] = "false";


  return IDAction - 1;
}

int MiraPanelServer::IndexAddLittleButton(String Name, bool Return_Line) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "little_button";
  if (Return_Line) Action["return"] = "true";
  else Action["return"] = "false";


  return IDAction - 1;
}

int MiraPanelServer::AddHTTPAction(String Name) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "httpaction";
  return IDAction - 1;
}



void MiraPanelServer::IndexAddTitle(String Name, String Size) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["nom"] = Name;
  Action["type"] = "title";
  Action["size"] = Size;
}

void MiraPanelServer::IndexAddText(String Name, String Size) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["nom"] = Name;
  Action["type"] = "text_index";
  Action["size"] = Size;
}

int MiraPanelServer::IndexAddTooltip(String Name, String Content, bool Return_Line) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "tooltip";
  Action["content"] = Content;
  if (Return_Line) Action["return"] = "true";
  else Action["return"] = "false";

  return IDAction - 1;
}

int MiraPanelServer::IndexAddRange(String Name, int Min, int Max, int Default) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "range";
  Action["value"] = Default;
  Action["min"] = Min;
  Action["max"] = Max;
  return IDAction - 1;
}



void MiraPanelServer::IndexSetRange(int ID, float Value) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      SPIFFS_Config_Actions["actions"][i]["value"] = Value;
      char cID[5] = "";
      sprintf(cID, "%d", ID);

      String SValue = (fabsf(Value - roundf(Value)) < 0.001f) ? String((int)roundf(Value)) : String(Value, 1);
      char CValue[50];
      SValue.toCharArray(CValue, 50);
      events.send(CValue, cID, millis());
      break;
    }
  }
}

float MiraPanelServer::IndexGetRange(int ID) {
  float Val = 0.0f;
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID) && SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "range") {
      Val = SPIFFS_Config_Actions["actions"][i]["value"].as<String>().toFloat();
      return Val;
      break;
    }
  }
  return Val;
}

int MiraPanelServer::IndexAddInputBox(String Name, String Type) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "inputbox";
  Action["typebox"] = Type;
  Action["value"] = "";
  return IDAction - 1;
}

void MiraPanelServer::IndexSetInputBox(int ID, String Value) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      SPIFFS_Config_Actions["actions"][i]["value"] = Value;
      char cID[5] = "";
      sprintf(cID, "%d", ID);

      char CValue[50];
      Value.toCharArray(CValue, 50);
      events.send(CValue, cID, millis());
      break;
    }
  }
}

String MiraPanelServer::IndexGetInputBox(int ID) {
  String retour = "null";
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      retour = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();

      break;
    }
  }
  return retour;
}

int MiraPanelServer::IndexAddInfo(String Name, String Unit, String Size) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "info";
  Action["value"] = "";
  Action["size"] = Size;
  Action["unit"] = Unit;
  return IDAction - 1;
}

void MiraPanelServer::IndexSetInfo(int ID, String Value) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      Action = ID;
      SPIFFS_Config_Actions["actions"][i]["value"] = Value;
      char cID[5] = "";
      sprintf(cID, "%d", ID);

      char CValue[50];
      Value.toCharArray(CValue, 50);
      events.send(CValue, cID, millis());
      break;
    }
  }
  //if (JEEDOM_mira) JEEDOM_Moment_Memorise = millis();
}

String MiraPanelServer::IndexGetInfo(int ID) {
  String retour = "null";
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      retour = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();

      break;
    }
  }
  return retour;
}

/*int MiraPanelServer::IndexAddInfoFA(String Name, String Size) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["type"] = "infofa";
  Action["value"] = "";
  Action["size"] = Size;
  Action["nom"] = Name;
  return IDAction - 1;
}



void MiraPanelServer::IndexSetInfoFA(int ID, String Value) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      SPIFFS_Config_Actions["actions"][i]["value"] = Value;
      char cID[5] = "";
      sprintf(cID, "%d", ID);

      char CValue[50];
      Value.toCharArray(CValue, 50);
      events.send(CValue, cID, millis());
      break;
    }
   
  }
}*/

void MiraPanelServer::IndexAddHTML(String Value) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = Value;
  Action["type"] = "html";
}

void MiraPanelServer::IndexBackgroundStart() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "<div class=\"FondCouleur\" style=\"margin-top:30px; margin-left:10px; margin-right:10px; padding-top:1px; border-radius:20px;\">";
  Action["type"] = "html";
}

void MiraPanelServer::IndexBackgroundFinish() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "</div>";
  Action["type"] = "html";
}

void MiraPanelServer::IndexTileStart(String Width, String Height, String HTML_Color) {
  if (HTML_Color == "" || HTML_Color == "Defaut") HTML_Color = DarkColor;
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  if (Height == "" || Height == "Defaut") {
    Action["value"] = "<div class=\"tuile\" style=\" background: " + HTML_Color + "; width:" + Width + ";\">";
  } else {
    String Temp = "<div class=\"tuile\" style=\" background: " + HTML_Color + "; width:" + Width + "; height:" + Height + ";\">";
    Action["value"] = Temp;
  }
  Action["type"] = "html";
}

void MiraPanelServer::IndexTileWhiteStart(String Width, String Height, String HTML_Border_Color) {
  if (HTML_Border_Color == "" || HTML_Border_Color == "Defaut") HTML_Border_Color = DarkColor;
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  // Action["id"] = IDAction;
  //IDAction++;
  if (Height == "" || Height == "Defaut") {
    Action["value"] = "<div class=\"tuile_white\" style=\"border: 5px solid " + HTML_Border_Color + "; width:" + Width + ";\">";
  } else {

    String Temp = "<div class=\"tuile_white\" style=\"border: 5px solid " + HTML_Border_Color + "; width:" + Width + "; height:" + Height + "; \">";
    Action["value"] = Temp;
  }
  Action["type"] = "html";
}

void MiraPanelServer::IndexTileFinish() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "</div>";
  Action["type"] = "html";
}

void MiraPanelServer::IndexContainerStart(int Width) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "<div class=\"container\" style=\"width:" + String(Width) + "px;\">";
  Action["type"] = "html";
}

void MiraPanelServer::IndexContainerFinish() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "</div>";
  Action["type"] = "html";
}

void MiraPanelServer::IndexTableStart() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "<table class=\"table_container\">";
  Action["type"] = "html";
}

void MiraPanelServer::IndexTableFinish() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "</table>";
  Action["type"] = "html";
}

void MiraPanelServer::IndexRowStart() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "<tr>";
  Action["type"] = "html";
}

void MiraPanelServer::IndexRowFinish() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "</tr>";
  Action["type"] = "html";
}

void MiraPanelServer::IndexCellStart(int Colspan, int Rowspan, String Align, String Width, String Height, bool Border) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  if (Align == "") Align = "top";
  if (Border) {
    Action["value"] = "<td width=" + Width + " height=" + Height + " rowspan=\"" + String(Rowspan) + "\" colspan =\"" + String(Colspan) + "\" style=\"padding:0px; border: 1px solid " + DarkColor + "; vertical-align : " + Align + ";\">";

  } else {
    Action["value"] = "<td width=" + Width + " height=" + Height + " rowspan=\"" + String(Rowspan) + "\" colspan =\"" + String(Colspan) + "\" style=\"padding:0px; vertical-align : " + Align + ";\">";
  }
  Action["type"] = "html";
}

void MiraPanelServer::IndexCellFinish() {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  //Action["id"] = IDAction;
  //IDAction++;
  Action["value"] = "</td>";
  Action["type"] = "html";
}

int MiraPanelServer::IndexAddColor(String Name, int Default_R, int Default_G, int Default_B) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "color";
  Action["r"] = String(Default_R);
  Action["g"] = String(Default_G);
  Action["b"] = String(Default_B);
  return IDAction - 1;
}

void MiraPanelServer::IndexSetColor(int ID, int R, int G, int B) {


  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      SPIFFS_Config_Actions["actions"][i]["r"] = String(R);
      SPIFFS_Config_Actions["actions"][i]["g"] = String(G);
      SPIFFS_Config_Actions["actions"][i]["b"] = String(B);
      String SNom = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
      char CNom[50];
      SNom.toCharArray(CNom, 50);
      long r = 0;
      r = (long)R;
      long g = 0;
      g = (long)G;
      long b = 0;
      b = (long)B;

      String SValue = "#000000";
      char CValue[7];
      sprintf(CValue, "#%02x%02x%02x", r, g, b);

      events.send(CValue, CNom, millis());

      break;
    }
  }
}





int MiraPanelServer::IndexGetColor_R(int ID) {
  int Val = 0;
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID) && SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "color") {
      Val = SPIFFS_Config_Actions["actions"][i]["r"].as<String>().toInt();
      return Val;
      break;
    }
  }
  return Val;
}

int MiraPanelServer::IndexGetColor_G(int ID) {
  int Val = 0;
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID) && SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "color") {
      Val = SPIFFS_Config_Actions["actions"][i]["g"].as<String>().toInt();
      return Val;
      break;
    }
  }
  return Val;
}

int MiraPanelServer::IndexGetColor_B(int ID) {
  int Val = 0;
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID) && SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "color") {
      Val = SPIFFS_Config_Actions["actions"][i]["b"].as<String>().toInt();
      return Val;
      break;
    }
  }
  return Val;
}



int* MiraPanelServer::IndexAddToogle(String Name, String Value_0, String Value_1, String Default) {
  if (Default == Value_0) Default = "Off";
  else Default = "On";

  Value_0 = "Off";
  Value_1 = "On";

  JsonArray Actions = SPIFFS_Config_Actions["actions"];

  static int ID[2];
  JsonObject Action = Actions.createNestedObject();
  Action["id0"] = IDAction;
  ID[0] = IDAction;
  IDAction++;
  Action["id1"] = IDAction;
  ID[1] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "toggle";
  Action["value0"] = Value_0;
  Action["value1"] = Value_1;
  Action["state"] = Default;

  return ID;
}

int* MiraPanelServer::IndexAddToggle(String Name, String Value_0, String Value_1, String Default) {
  if (Default == Value_0) Default = "Off";
  else Default = "On";

  Value_0 = "Off";
  Value_1 = "On";

  JsonArray Actions = SPIFFS_Config_Actions["actions"];

  static int ID[2];
  JsonObject Action = Actions.createNestedObject();
  Action["id0"] = IDAction;
  ID[0] = IDAction;
  IDAction++;
  Action["id1"] = IDAction;
  ID[1] = IDAction;
  IDAction++;
  Action["nom"] = Name;
  Action["type"] = "toggle";
  Action["value0"] = Value_0;
  Action["value1"] = Value_1;
  Action["state"] = Default;

  return ID;
}

String MiraPanelServer::IndexGetToogle(int ID) {
  String retour = "null";
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      retour = SPIFFS_Config_Actions["actions"][i]["state"].as<String>();

      break;
    }
  }
  return retour;
}

String MiraPanelServer::IndexGetToggle(int ID) {
  String retour = "null";
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      retour = SPIFFS_Config_Actions["actions"][i]["state"].as<String>();

      break;
    }
  }
  return retour;
}



void MiraPanelServer::IndexSetToogle(int ID) {
  //ActionGet = false;
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() == String(ID) || SPIFFS_Config_Actions["actions"][i]["id1"].as<String>() == String(ID)) {
      String Value;
      if (SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() == String(ID)) {
        Value = SPIFFS_Config_Actions["actions"][i]["value0"].as<String>();
        SPIFFS_Config_Actions["actions"][i]["state"] = Value;
      } else if (SPIFFS_Config_Actions["actions"][i]["id1"].as<String>() == String(ID)) {
        Value = SPIFFS_Config_Actions["actions"][i]["value1"].as<String>();
        SPIFFS_Config_Actions["actions"][i]["state"] = Value;
      }

      char cID[5] = "";
      sprintf(cID, "%d", ID);

      char CValue[50];
      Value.toCharArray(CValue, 50);
      events.send(CValue, cID, millis());

      break;
    }
  }
}

void MiraPanelServer::IndexSetToggle(int ID) {
  //ActionGet = false;
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() == String(ID) || SPIFFS_Config_Actions["actions"][i]["id1"].as<String>() == String(ID)) {
      String Value;
      if (SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() == String(ID)) {
        Value = SPIFFS_Config_Actions["actions"][i]["value0"].as<String>();
        SPIFFS_Config_Actions["actions"][i]["state"] = Value;
      } else if (SPIFFS_Config_Actions["actions"][i]["id1"].as<String>() == String(ID)) {
        Value = SPIFFS_Config_Actions["actions"][i]["value1"].as<String>();
        SPIFFS_Config_Actions["actions"][i]["state"] = Value;
      }

      char cID[5] = "";
      sprintf(cID, "%d", ID);

      char CValue[50];
      Value.toCharArray(CValue, 50);
      events.send(CValue, cID, millis());

      break;
    }
  }
}


int MiraPanelServer::IndexAddList(String NameList, String Name, String Option) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  JsonObject Action = Actions.createNestedObject();
  Action["id"] = IDAction;
  IDAction++;
  Action["nomliste"] = NameList;
  Action["nom"] = Name;
  Action["type"] = "list";
  Action["option"] = Option;


  bool Liste_Existante = false;
  for (int k = 0; k < NB_Listes; k++) {
    if (Listes[k] == NameList) {
      Liste_Existante = true;
      Action["value"] = Listes_Value[k];
      break;
    }
  }

  if (!Liste_Existante) {
    Listes[NB_Listes] = NameList;
    Listes_Value[NB_Listes] = IDAction - 1;

    NB_Listes++;
    Action["value"] = IDAction - 1;
  }


  return IDAction - 1;
}

void MiraPanelServer::IndexSetList(int ID) {



  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
      String Value = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
      String NomListe = SPIFFS_Config_Actions["actions"][i]["nomliste"].as<String>();
      for (int j = 0; j < Actions.size(); j++) {
        if (SPIFFS_Config_Actions["actions"][i]["nomliste"].as<String>() == SPIFFS_Config_Actions["actions"][j]["nomliste"].as<String>()) {
          SPIFFS_Config_Actions["actions"][j]["value"] = Value;
        }
      }



      String SNom = NomListe;
      char CNom[50];
      SNom.toCharArray(CNom, 50);
      char CValue[50];
      String SValue = String(ID);
      SValue.toCharArray(CValue, 50);
      events.send(CNom, CValue, millis());

      for (int k = 0; k < NB_Listes; k++) {
        if (Listes[k] == NomListe) {
          Listes_Value[k] = SValue;
        }
      }
      break;
    }
  }
}

int MiraPanelServer::IndexSetListByOption(String NameList, String Option) {



  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["nomliste"].as<String>() == NameList && SPIFFS_Config_Actions["actions"][i]["option"].as<String>() == Option) {
      String Value = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
      String NomListe = SPIFFS_Config_Actions["actions"][i]["nomliste"].as<String>();
      String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
      for (int j = 0; j < Actions.size(); j++) {
        if (SPIFFS_Config_Actions["actions"][i]["nomliste"].as<String>() == SPIFFS_Config_Actions["actions"][j]["nomliste"].as<String>()) {
          SPIFFS_Config_Actions["actions"][j]["value"] = Value;
        }
      }

      String SNom = NomListe;
      char CNom[50];
      SNom.toCharArray(CNom, 50);
      char CValue[50];
      String SValue = ID;
      SValue.toCharArray(CValue, 50);
      events.send(CNom, CValue, millis());

      for (int k = 0; k < NB_Listes; k++) {
        if (Listes[k] == NomListe) {
          Listes_Value[k] = SValue;
        }
      }
      return ID.toInt();
      break;
    }
  }
}

void MiraPanelServer::AddListParam(String Name, String Default, bool Hidden, bool Boolean) {
  SPIFFS_ADD_Param = Name;
  SPIFFS_ADD_Value = Default;
  if (Hidden) {
    SPIFFS_ADD_Type = "hidden-text";
  } else {
    if (Boolean) SPIFFS_ADD_Type = "toggle";
    else SPIFFS_ADD_Type = "text";
  }

  SPIFFS_AddListParam();
}

void MiraPanelServer::CreateListParam() {
  if (_Debug == true) Serial.println("[MIRA] Creation des parametres...");
  SPIFFS_SaveParam_List();
}

void MiraPanelServer::UpdateListParam(String Name, String Value) {
  SPIFFS_ADD_Param = Name;
  SPIFFS_ADD_Value = Value;

  SPIFFS_UpdateListParam();
}

void MiraPanelServer::SaveListParam() {
  SPIFFS_SaveParam_List();
}




void MiraPanelServer::ResetButton(int PinButton, int PinLed) {
  RST_Button = true;
  Bouton_RST = PinButton;
  Led_RST = PinLed;
  pinMode(Bouton_RST, INPUT_PULLUP);
  if (PinLed > 0) {
    pinMode(Led_RST, OUTPUT);
    digitalWrite(Led_RST, LOW);
  }
}


void MiraPanelServer::AddParamText(String Name, String Default, bool Hidden) {
  SPIFFS_ADD_Param = Name;
  SPIFFS_ADD_Value = Default;
  if (Hidden) SPIFFS_ADD_Type = "hidden-text";
  else SPIFFS_ADD_Type = "text";
  SPIFFS_AddParam();
}

void MiraPanelServer::AddParamBool(String Name, bool Default) {
  SPIFFS_ADD_Param = Name;
  SPIFFS_ADD_Value = String(Default);
  SPIFFS_ADD_Type = "toggle";
  SPIFFS_AddParam();
}

String MiraPanelServer::GetParamText(String Name) {
  JsonArray Params = SPIFFS_Config_Params["params"];
  if (Params.size() > 0) {
    for (int i = 0; i < Params.size(); i++) {
      if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == Name) {
        if (SPIFFS_Config_Params["params"][i]["type"].as<String>() == "crypted-text") {
          String Decrypt = SPIFFS_Uncrypt(SPIFFS_Config_Params["params"][i]["value"].as<String>());
          return Decrypt;
        } else {
          return SPIFFS_Config_Params["params"][i]["value"].as<String>();
        }

        break;
      }
      if (i == Params.size() - 1) {
        return "null";
      }
    }
  } else {
    return "null";
  }
}

bool MiraPanelServer::GetParamBool(String Name) {
  JsonArray Params = SPIFFS_Config_Params["params"];
  if (Params.size() > 0) {
    for (int i = 0; i < Params.size(); i++) {
      if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == Name) {
        if (SPIFFS_Config_Params["params"][i]["value"].as<String>() == "0") {
          return false;
        } else if (SPIFFS_Config_Params["params"][i]["value"].as<String>() == "1") {
          return true;
        } else {
          return false;
        }
        break;
      }
      if (i == Params.size() - 1) {
        return false;
      }
    }
  } else {
    return false;
  }
}

void MiraPanelServer::RemoveParam(String Name) {
  SPIFFS_ADD_Param = Name;
  SPIFFS_RemoveParam();
}

void MiraPanelServer::UpdateParamText(String Name, String Value) {
  SPIFFS_ADD_Param = Name;
  SPIFFS_ADD_Value = Value;
  SPIFFS_UpdateParam();
}

void MiraPanelServer::UpdateParamBool(String Name, bool Value) {
  SPIFFS_ADD_Param = Name;
  if (Value == true) {
    SPIFFS_ADD_Value = "1";
  } else {
    SPIFFS_ADD_Value = "0";
  }

  SPIFFS_UpdateParam();
}


int MiraPanelServer::GetWiFiStatus() {
  if (ModeAP == true && ConnexionPerdue == false) {
    return 2;
  }
  if (ModeAP == false && ConnexionPerdue == false) {
    return 1;
  }
  if (ConnexionPerdue == true) {
    return 3;
  }
}

bool MiraPanelServer::IsSetupMode() {
  return ModeAP;
}


bool MiraPanelServer::IsUpdateInProgress() {
  return Update.isRunning();
}


void MiraPanelServer::ScanWiFi() {
  //Scan des réseaux wifi
  if (_Debug == true) Serial.println(F("[MIRA] Scan des réseaux Wi-Fi alentours"));
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  if (n != 0) {
    for (int i = 0; i < n; i++) {
      if (i < 9) {
        ScanReseaux[i] = WiFi.SSID(i);
      }
    }
  }
}



bool MiraPanelServer::initPA() {
  if (_Debug == true) Serial.println(F("[MIRA] Configuration du point d'acces : "));

  IPAddress AP_local_ip;
  AP_local_ip.fromString(SPIFFS_Config_Reseau["ap"][0]["ip"].as<String>());
  delay(200);
  IPAddress AP_gateway(192, 168, 1, 1);
  IPAddress AP_subnet(255, 255, 255, 0);
  WiFi.mode(WIFI_AP_STA);
  delay(200);
  WiFi.softAPConfig(AP_local_ip, AP_gateway, AP_subnet);


  String SPass = Uncrypt_Pass;
  char CPass[100];
  SPass.toCharArray(CPass, 100);

  WiFi.softAP(NomProg, CPass);
  IPAddress IP = WiFi.softAPIP();
  if (_Debug == true) Serial.print(F("[MIRA] Adresse IP point d'acces : "));
  if (_Debug == true) Serial.println(IP);
  ModeAP = true;
  return true;
}


static String miraFormValue(AsyncWebServerRequest* req, const char* key,
                             const String& fallback = "") {
  return req->hasParam(key, true) ? req->getParam(key, true)->value() : fallback;
}
static bool miraValidIP(const String& v) {
  IPAddress a;
  return a.fromString(v) && a[0] > 0 && a[0] < 224 && a[0] != 127;
}
static bool miraValidMask(const String& v) {
  IPAddress a;
  if (!a.fromString(v)) return false;
  uint32_t n = ((uint32_t)a[0]<<24) | ((uint32_t)a[1]<<16) |
               ((uint32_t)a[2]<<8) | a[3];
  if (!n || n == 0xFFFFFFFFUL) return false;
  uint32_t inv = ~n;
  return (inv & (inv + 1U)) == 0;
}
static bool miraSameSubnet(const IPAddress& a,const IPAddress& b,const IPAddress& m) {
  for (int i=0;i<4;i++) if ((a[i]&m[i])!=(b[i]&m[i])) return false;
  return true;
}
static String miraWifiMode(JsonVariantConst cfg) {
  String m = cfg["ip_mode"] | "";
  if (m != "auto" && m != "fixed_auto" && m != "manual") {
    String ip = cfg["ip"] | "";
    m = (!ip.length() || ip == "Auto" || ip == "null") ? "auto" : "fixed_auto";
  }
  return m;
}
static bool miraReadNetworkForm(AsyncWebServerRequest* req, String& err) {
  String ip = miraFormValue(req,"ip","Auto"); ip.trim();
  String mode = miraFormValue(req,"ip_mode");
  if (!mode.length()) mode=(!ip.length() || ip=="Auto") ? "auto":"fixed_auto";
  if (mode!="auto" && mode!="fixed_auto" && mode!="manual") {
    err="Mode IP invalide"; return false;
  }
  String mask=miraFormValue(req,"mask"),gw=miraFormValue(req,"gateway"),
         dns=miraFormValue(req,"dns1"),dns2=miraFormValue(req,"dns2");
  mask.trim();gw.trim();dns.trim();dns2.trim();
  if (mode!="auto" && !miraValidIP(ip)) {
    err="Adresse IPv4 invalide";return false;
  }
  if (mode=="manual") {
    if(!miraValidMask(mask) || !miraValidIP(gw) || !miraValidIP(dns) ||
       (dns2.length()&&!miraValidIP(dns2))) {
      err="Masque, passerelle ou DNS invalide"; return false;
    }
    IPAddress ia,ga,ma;
    ia.fromString(ip);ga.fromString(gw);ma.fromString(mask);
    if (!miraSameSubnet(ia,ga,ma) || ia==ga) {
      err="IP et passerelle doivent etre dans le meme reseau et differentes";
      return false;
    }
  } else { mask="";gw="";dns="";dns2=""; }
  SPIFFS_IP_Mode=mode;SPIFFS_IP=(mode=="auto")?"Auto":ip;
  SPIFFS_Mask=mask;SPIFFS_Gateway=gw;SPIFFS_DNS1=dns;SPIFFS_DNS2=dns2;
  return true;
}
static void miraUseDhcp() {
  IPAddress z(0,0,0,0);
  WiFi.config(z,z,z,z,z);
}
static void miraPrepareWifiProfile(JsonVariantConst cfg) {
  WiFi.mode(WIFI_STA);
  if (miraWifiMode(cfg)=="manual") {
    String a=cfg["ip"]|"", g=cfg["gateway"]|"", m=cfg["mask"]|"",
           d=cfg["dns1"]|"", d2=cfg["dns2"]|"";
    if (miraValidIP(a)&&miraValidIP(g)&&miraValidMask(m)&&miraValidIP(d)) {
      IPAddress ip,gw,mask,dns,dns2(0,0,0,0);
      ip.fromString(a);gw.fromString(g);mask.fromString(m);dns.fromString(d);
      if(miraValidIP(d2))dns2.fromString(d2);
      if (miraSameSubnet(ip,gw,mask)&&ip!=gw&&
          WiFi.config(ip,gw,mask,dns,dns2)) {
        Serial.printf("[MIRA][WIFI] Statique : %s, DNS %s\n",
                      ip.toString().c_str(), dns.toString().c_str());
        return;
      }
    }
    Serial.println("[MIRA][WIFI] Parametres statiques invalides, DHCP");
  }
  miraUseDhcp();
}
static void miraFinishWifiProfile(JsonVariantConst cfg) {
  if(miraWifiMode(cfg)!="fixed_auto")return;
  String raw=cfg["ip"]|"";
  IPAddress chosen;
  if(!miraValidIP(raw)||!chosen.fromString(raw))return;
  IPAddress gateway=WiFi.gatewayIP(),mask=WiFi.subnetMask();
  IPAddress dns1=WiFi.dnsIP(0),dns2=WiFi.dnsIP(1);
  if(!miraValidMask(mask.toString())||
     !miraSameSubnet(chosen,gateway,mask)||
     !miraSameSubnet(chosen,WiFi.localIP(),mask)||chosen==gateway) {
    Serial.println("[MIRA][WIFI] IP hors reseau DHCP : DHCP conserve");
    return;
  }
  if(chosen==WiFi.localIP())return;
  if(!WiFi.config(chosen,gateway,mask,dns1,dns2)) {
    Serial.println("[MIRA][WIFI] Echec IP fixe : retour DHCP");
    miraUseDhcp();return;
  }
  Serial.printf("[MIRA][WIFI] IP fixe %s, options DHCP conservees\n",
                chosen.toString().c_str());
}

bool MiraPanelServer::initWiFi() {
  if (_Debug == true) Serial.println(F("[MIRA] Initialisation du Wi-Fi"));

  if (SPIFFS_Config_Reseau["reseau"][0]["ssid"] == "null") {
    return false;
  }
  WiFi.setTxPower(WIFI_POWER_19_5dBm);


  JsonArray reseaux = SPIFFS_Config_Reseau["reseau"];

  for (int scan = 0; scan < 10; scan++) {
    if (ScanReseaux[scan] != "") {
      for (int par = reseaux.size() - 1; par >= 0; par--) {
        String SSIDpar = SPIFFS_Config_Reseau["reseau"][par]["ssid"];
        if (SSIDpar == ScanReseaux[scan]) {
          String PASSpar = SPIFFS_Uncrypt(SPIFFS_Config_Reseau["reseau"][par]["pass"]);

          miraPrepareWifiProfile(SPIFFS_Config_Reseau["reseau"][par]);

          WiFi.begin(SSIDpar.c_str(), PASSpar.c_str());
          if (_Debug == true) Serial.print(F("[MIRA] M2: Tentative de connexion a : "));
          if (_Debug == true) Serial.println(SSIDpar);

          if (WiFiConnect()) {

            miraFinishWifiProfile(SPIFFS_Config_Reseau["reseau"][par]);
            if (_Debug == true) Serial.print(F("[MIRA] M2: Connexion reussie a : "));
            if (_Debug == true) Serial.println(SSIDpar);
            if (_Debug == true) Serial.print(F("[MIRA] M2: Adresse IP: "));
            if (_Debug == true) Serial.println(WiFi.localIP());
            StringAdresseIP = WiFi.localIP().toString();
            ModeAP = false;
            if (_Debug == true) Serial.print(F("[MIRA] Signal: "));
            if (_Debug == true) Serial.println(WiFi.RSSI());
            return true;
          } else {
            if (_Debug == true) Serial.print(F("[MIRA] M2: Connexion impossible a : "));
            if (_Debug == true) Serial.println(SSIDpar);
          }
        }
      }
    }
  }

  for (int i = reseaux.size() - 1; i >= 0; i--) {

    String SSID = SPIFFS_Config_Reseau["reseau"][i]["ssid"];
    String PASS = SPIFFS_Uncrypt(SPIFFS_Config_Reseau["reseau"][i]["pass"]);

    miraPrepareWifiProfile(SPIFFS_Config_Reseau["reseau"][i]);

    WiFi.begin(SSID.c_str(), PASS.c_str());
    if (_Debug == true) Serial.print(F("[MIRA] Tentative de connexion a : "));
    if (_Debug == true) Serial.println(SSID);

    if (WiFiConnect()) {

      miraFinishWifiProfile(SPIFFS_Config_Reseau["reseau"][i]);
      if (_Debug == true) Serial.print(F("[MIRA] Connexion reussie a : "));
      if (_Debug == true) Serial.println(SSID);
      if (_Debug == true) Serial.print(F("[MIRA] Adresse IP: "));
      if (_Debug == true) Serial.println(WiFi.localIP());
      StringAdresseIP = WiFi.localIP().toString();
      ModeAP = false;
      if (_Debug == true) Serial.print(F("[MIRA] Signal: "));
      if (_Debug == true) Serial.println(WiFi.RSSI());
      return true;
    } else {
      if (_Debug == true) Serial.print(F("[MIRA] Connexion impossible a : "));
      if (_Debug == true) Serial.println(SSID);
      if (i == 0) {
        return false;
      }
    }
  }
}

bool MiraPanelServer::ReInitWiFi() {
  if (_Debug == true) Serial.println(F("[MIRA] Initialisation du Wi-Fi"));

  String Conf = SPIFFS_Config_Reseau["reseau"][0]["ssid"];
  if (Conf == "null") {
    return false;
  }
  WiFi.setTxPower(WIFI_POWER_19_5dBm);


  JsonArray reseaux = SPIFFS_Config_Reseau["reseau"];

  for (int scan = 0; scan < 10; scan++) {
    if (ScanReseaux[scan] != "") {
      for (int par = reseaux.size() - 1; par >= 0; par--) {
        String SSIDpar = SPIFFS_Config_Reseau["reseau"][par]["ssid"];
        if (SSIDpar == ScanReseaux[scan]) {
          String PASSpar = SPIFFS_Uncrypt(SPIFFS_Config_Reseau["reseau"][par]["pass"]);

          miraPrepareWifiProfile(SPIFFS_Config_Reseau["reseau"][par]);

          WiFi.begin(SSIDpar.c_str(), PASSpar.c_str());
          if (_Debug == true) Serial.print(F("[MIRA] M2: Tentative de connexion a : "));
          if (_Debug == true) Serial.println(SSIDpar);

          if (WiFiConnect()) {

            miraFinishWifiProfile(SPIFFS_Config_Reseau["reseau"][par]);
            if (_Debug == true) Serial.print(F("[MIRA] M2: Connexion reussie a : "));
            if (_Debug == true) Serial.println(SSIDpar);
            if (_Debug == true) Serial.print(F("[MIRA] M2: Adresse IP: "));
            if (_Debug == true) Serial.println(WiFi.localIP());
            StringAdresseIP = WiFi.localIP().toString();
            ModeAP = false;
            if (_Debug == true) Serial.print(F("[MIRA] Signal: "));
            if (_Debug == true) Serial.println(WiFi.RSSI());
            return true;
          } else {
            if (_Debug == true) Serial.print(F("[MIRA] M2: Connexion impossible a : "));
            if (_Debug == true) Serial.println(SSIDpar);
          }
        }
      }
    }
  }

  for (int i = reseaux.size() - 1; i >= 0; i--) {

    String SSID = SPIFFS_Config_Reseau["reseau"][i]["ssid"];
    String PASS = SPIFFS_Uncrypt(SPIFFS_Config_Reseau["reseau"][i]["pass"]);

    miraPrepareWifiProfile(SPIFFS_Config_Reseau["reseau"][i]);

    WiFi.begin(SSID.c_str(), PASS.c_str());
    if (_Debug == true) Serial.print(F("[MIRA] Tentative de connexion a : "));
    if (_Debug == true) Serial.println(SSID);

    if (WiFiConnect()) {

      miraFinishWifiProfile(SPIFFS_Config_Reseau["reseau"][i]);
      if (_Debug == true) Serial.print(F("[MIRA] Connexion reussie a : "));
      if (_Debug == true) Serial.println(SSID);
      if (_Debug == true) Serial.print(F("[MIRA] Adresse IP: "));
      if (_Debug == true) Serial.println(WiFi.localIP());
      StringAdresseIP = WiFi.localIP().toString();
      ModeAP = false;
      if (_Debug == true) Serial.print(F("[MIRA] Signal: "));
      if (_Debug == true) Serial.println(WiFi.RSSI());
      return true;
    } else {
      if (_Debug == true) Serial.print(F("[MIRA] Connexion impossible a : "));
      if (_Debug == true) Serial.println(SSID);
      if (i == 0) {
        return false;
      }
    }
  }
}

bool MiraPanelServer::WiFiConnect() {
  unsigned long currentMillis = millis();
  previousMillis = currentMillis;
  while (WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= timeout) {
      return false;
    }
    delay(100);
  }
  ModeAP = false;
  return true;
}




String MiraPanelServer::RetourVariable(String var) {
  String RETOUR = "Null";

  auto GetLocalParamValue = [](const String& name) -> String {
    JsonArray Params = SPIFFS_Config_Params["params"];
    for (int i = 0; i < Params.size(); ++i) {
      if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == name) {
        return SPIFFS_Config_Params["params"][i]["value"].as<String>();
      }
    }
    return "";
  };

  if (var == "MQTTACTIF") {
    String v = GetLocalParamValue("MQTT_Actif");
    return (v == "1" || v == "true") ? "1" : "0";
  }
  if (var == "MQTTCHECKED") {
    String v = GetLocalParamValue("MQTT_Actif");
    return (v == "1" || v == "true") ? "checked" : "";
  }
  if (var == "MQTTNOM") return GetLocalParamValue("MQTT_Nom");
  if (var == "MQTTSERVEUR") return GetLocalParamValue("MQTT_Serveur");
  if (var == "MQTTPORT") return GetLocalParamValue("MQTT_Port");
  if (var == "MQTTUSER") return GetLocalParamValue("MQTT_Utilisateur");
  if (var == "MQTTPASS") return GetLocalParamValue("MQTT_Mot_de_passe");
  if (var == "MQTTTABLE") {
    RETOUR = "";
    JsonArray Json_Array = mira_Json_LVGL["lvgl"];
    for (int i = 0; i < Json_Array.size(); ++i) {
      String nom = mira_Json_LVGL["lvgl"][i]["name"].as<String>();
      String type = mira_Json_LVGL["lvgl"][i]["type"].as<String>();
      if (!nom.length() && type == "datetime") nom = "Date et heure";
      String typeLabel = type;
      if (type == "button" || type == "little_button") typeLabel = "Bouton";
      else if (type == "toggle" || type == "toogle") typeLabel = "Interrupteur";
      else if (type == "range" || type == "roundrange") typeLabel = "Curseur";
      else if (type == "list" || type == "listbutton") typeLabel = "Liste";
      else if (type == "color") typeLabel = "Couleur";
      else if (type == "info") typeLabel = "Information";
      else if (type == "datetime") typeLabel = "Date / heure";
      RETOUR += "<tr><td>" + nom + "</td><td>" + typeLabel + "</td></tr>";
    }
    if (!RETOUR.length()) RETOUR = "<tr><td colspan='2'>Aucun widget programme</td></tr>";
    return RETOUR;
  }

  if (var == "VERSION") {
    if (MiraRuntimeVersion.length()) return MiraRuntimeVersion;
    String v = SPIFFS_Config_Module["module"][0]["version"].as<String>();
    if (v.length()) v = SPIFFS_Uncrypt(v);
    return v.length() ? v : "0.4.11";
  }
  if (var == "LOCALIP") {
    if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
    return StringAdresseIP.length() ? StringAdresseIP : "Non connecte";
  }

  if (var == "MAJ") {
    if (SetupModuleDone)
      return RETOUR = "<button class=\"Bouton\" onclick=\"window.location.href = '/load?nav=checkupdate';\"style=\"text-decoration:none\"> Vérifier les mises à jour</button>";
    else {
      return RETOUR = "";
    }
  }
  if (var == "KEY") RETOUR = Uncrypt_Pass;

  if (var == "SSIDUPDATE") {
    JsonArray Reseaux = SPIFFS_Config_Reseau["reseau"];
    for (int i = 0; i < Reseaux.size(); i++) {
      if (SPIFFS_Config_Reseau["reseau"][i]["id"].as<String>() == IDNetwork) {
        return RETOUR = SPIFFS_Config_Reseau["reseau"][i]["ssid"].as<String>();
        break;
      }
    }
  }

  if (var == "PASSUPDATE") {
    JsonArray Reseaux = SPIFFS_Config_Reseau["reseau"];
    for (int i = 0; i < Reseaux.size(); i++) {
      if (SPIFFS_Config_Reseau["reseau"][i]["id"].as<String>() == IDNetwork) {
        return RETOUR = SPIFFS_Uncrypt(SPIFFS_Config_Reseau["reseau"][i]["pass"].as<String>());
        break;
      }
    }
  }

  // Network form placeholders (new profile and editing an existing SSID).
  if (var == "IPMODE") return SPIFFS_IP_Mode.length() ? SPIFFS_IP_Mode : "auto";
  if (var == "MASK") return SPIFFS_Mask;
  if (var == "GATEWAY") return SPIFFS_Gateway;
  if (var == "DNS1") return SPIFFS_DNS1;
  if (var == "DNS2") return SPIFFS_DNS2;
  if (var == "WIFIACTIVE") {
    if (WiFi.status() != WL_CONNECTED) return "Wi-Fi non connecte";
    return String("IP ") + WiFi.localIP().toString() +
           " | Masque " + WiFi.subnetMask().toString() +
           " | Passerelle " + WiFi.gatewayIP().toString() +
           " | DNS " + WiFi.dnsIP(0).toString() +
           " / " + WiFi.dnsIP(1).toString();
  }
  if (var == "IPMODEUPDATE" || var == "MASKUPDATE" ||
      var == "GATEWAYUPDATE" || var == "DNS1UPDATE" || var == "DNS2UPDATE") {
    JsonArray nets = SPIFFS_Config_Reseau["reseau"];
    for (JsonVariant network : nets) {
      if (network["id"].as<String>() != IDNetwork) continue;
      if (var == "IPMODEUPDATE") return miraWifiMode(network);
      String key = var == "MASKUPDATE" ? "mask" :
                   var == "GATEWAYUPDATE" ? "gateway" :
                   var == "DNS1UPDATE" ? "dns1" : "dns2";
      return network[key] | "";
    }
    return var == "IPMODEUPDATE" ? "auto" : "";
  }

  if (var == "IPUPDATE") {
    JsonArray Reseaux = SPIFFS_Config_Reseau["reseau"];
    for (int i = 0; i < Reseaux.size(); i++) {
      if (SPIFFS_Config_Reseau["reseau"][i]["id"].as<String>() == IDNetwork) {
        return RETOUR = SPIFFS_Config_Reseau["reseau"][i]["ip"].as<String>();
        break;
      }
    }
  }

  if (var == "RESEAUXENREG") {
    RETOUR = "";
    JsonArray Reseaux = SPIFFS_Config_Reseau["reseau"];
    if (Reseaux.size() == 0) {
      RETOUR = "<div class=\"empty\">Aucun réseau n'est enregistré.</div>";
    } else {
      for (int i = 0; i < Reseaux.size(); i++) {
        String id = SPIFFS_Config_Reseau["reseau"][i]["id"].as<String>();
        String ssid = SPIFFS_Config_Reseau["reseau"][i]["ssid"].as<String>();
        RETOUR += "<div class=\"network-item\"><span class=\"network-id\">" + id + "</span>";
        RETOUR += "<button class=\"network-button\" onclick=\"window.location.href='/load?nav=update&network=" + id + "'\">" + ssid + "</button></div>";
      }
    }
    return RETOUR;
  }

  if (var == "TITRE") {
    return RETOUR = TITRE;
  }

  if (var == "MESSAGE") {
    ScriptConstruct = "";
    ScriptConstruct += "<script>";
    ScriptConstruct += "if (!!window.EventSource) {";
    ScriptConstruct += "var source = new EventSource('/events');";
    ScriptConstruct += "source.addEventListener('open', function(e) {";
    ScriptConstruct += "console.log(\"Events Connected\");";
    ScriptConstruct += "}, false);";
    ScriptConstruct += "source.addEventListener('error', function(e) {";
    ScriptConstruct += "if (e.target.readyState != EventSource.OPEN) {";
    ScriptConstruct += "console.log(\"Events Disconnected\");";
    ScriptConstruct += "}";
    ScriptConstruct += "}, false);";
    ScriptConstruct += "source.addEventListener('message', function(e) {";
    ScriptConstruct += "console.log(\"message\", e.data);";
    ScriptConstruct += "}, false);";
    ScriptConstruct += "source.addEventListener('divmessage', function(e) {document.getElementById(\"divmessage\").innerHTML = e.data;}, false);";
    ScriptConstruct += "}</script>";
    RETOUR = "<div id=\"divmessage\">" + MESSAGE + "</div>";
    RETOUR += ScriptConstruct;
    return RETOUR;
  }

  if (var == "SCANRESEAUX") {
    RETOUR = "<label for=\"RS\">Réseaux détectés</label><select id=\"RS\" name=\"RS\" class=\"box\" onchange=\"Click(this.value)\">";
    for (int j = 0; j < 10; j++) {
      if (ScanReseaux[j] != "") {
        RETOUR += "<option value=\"" + ScanReseaux[j] + "\">" + ScanReseaux[j] + "</option>";
      }
    }
    RETOUR += "</select>";
    return RETOUR;
  }


  if (var == "APP") {
    RETOUR = NomProg;
    return RETOUR;
  }

  if (var == "SSID") {
    if (SPIFFS_SSID == "null") {
      RETOUR = "";
    } else {
      RETOUR = SPIFFS_SSID;
    }
    return RETOUR;
  }

  if (var == "IP") {
    if (SPIFFS_IP == "null") {
      RETOUR = "Auto";
    } else {
      RETOUR = SPIFFS_IP;
    }
    return RETOUR;
  }

  if (var == "DARKCOLOR") {
    RETOUR = DarkColor;
    return RETOUR;
  }

  if (var == "LIGHTCOLOR") {
    RETOUR = LightColor;
    return RETOUR;
  }

  if (var == "ACTIVECOLOR") {
    RETOUR = ActiveColor;
    return RETOUR;
  }

  if (var == "TEXTCOLOR") {
    RETOUR = TextColor;
    return RETOUR;
  }

  if (var == "POWERED") {
    RETOUR = Powered;
    return RETOUR;
  }

  if (var == "LITTLELOGO") {
    if (Little_Logo_URL != "") {
      RETOUR = "<div class=\"FondLittleLogo\"> <img style=\"width:" + String(Little_Logo_Width) + "px; height:" + String(Little_Logo_Height) + "px;\" src=\"" + Little_Logo_URL + "\"> </div>";
    } else {
      RETOUR = "";
    }
    return RETOUR;
  }

  if (var == "LOGO") {
    if (Logo_URL != "") {
      RETOUR = "<div class=\"FondLogo\"> <img style=\"width:" + String(Logo_Width) + "px; height:" + String(Logo_Height) + "px;\" src=\"" + Logo_URL + "\"> </div>";
    } else {
      RETOUR = "";
    }
    return RETOUR;
  }



  if (var == "DISPLAYDAY") return GetLocalParamValue("DISPLAY_Jour");
  if (var == "DISPLAYNIGHT") return GetLocalParamValue("DISPLAY_Nuit");
  if (var == "DISPLAYSLEEPBRIGHT") return GetLocalParamValue("DISPLAY_Veille");
  if (var == "DISPLAYSLEEPTIMEOUT") return GetLocalParamValue("DISPLAY_Delai");
  if (var == "DISPLAYNIGHTSTART") {
    String value = GetLocalParamValue("DISPLAY_DebutNuit");
    value.trim();
    value.replace("%3A", ":");
    value.replace("%3a", ":");
    if (value.length() == 4 && value.indexOf(':') < 0) value = value.substring(0, 2) + ":" + value.substring(2);
    return value;
  }
  if (var == "DISPLAYNIGHTEND") {
    String value = GetLocalParamValue("DISPLAY_FinNuit");
    value.trim();
    value.replace("%3A", ":");
    value.replace("%3a", ":");
    if (value.length() == 4 && value.indexOf(':') < 0) value = value.substring(0, 2) + ":" + value.substring(2);
    return value;
  }
  if (var == "DISPLAYNIGHTAUTO") return GetLocalParamValue("DISPLAY_NuitAuto");
  if (var == "DISPLAYCLOCKSLEEP") return GetLocalParamValue("DISPLAY_HorlogeVeille");
  if (var == "DISPLAYNIGHTCHECKED") return GetLocalParamValue("DISPLAY_NuitAuto") == "1" ? "checked" : "";
  if (var == "DISPLAYCLOCKCHECKED") return GetLocalParamValue("DISPLAY_HorlogeVeille") == "1" ? "checked" : "";

  if (var == "PARAMS") {
    RETOUR = "";
    bool hasHttpWidget = false;

    JsonArray Json_Array = mira_Json_LVGL["lvgl"];
    for (int i = 0; i < Json_Array.size(); i++) {
      String type = mira_Json_LVGL["lvgl"][i]["type"].as<String>();
      if (type != "datetime" && type != "info") {
        String name = mira_Json_LVGL["lvgl"][i]["name"].as<String>();
        String url = mira_Json_LVGL["lvgl"][i]["url"].as<String>();
        RETOUR += "<div class=\"field\"><label>" + name + "</label>";
        RETOUR += "<input id=\"" + name + "\" name=\"" + name + "\" value=\"" + url + "\" type=\"text\" placeholder=\"URL d'envoi d'état\"></div>";
        hasHttpWidget = true;
      }
    }

    if (!hasHttpWidget) {
      RETOUR = "<div class=\"empty\">Aucun widget avec retour HTTP n'est actuellement déclaré.</div>";
    }
    return RETOUR;
  }

  if (var == "ACTIFAP") {
    String Actif = SPIFFS_Config_Reseau["ap"][0]["actif"].as<String>();
    String Checked = (Actif == "1") ? " checked" : "";
    String Texte = (Actif == "1") ? "Actif" : "Inactif";
    RETOUR = "<div class=\"switchline\"><div><strong>Point d'accès</strong><div class=\"hint\" id=\"texte\" style=\"margin:3px 0 0\">" + Texte + "</div></div>";
    RETOUR += "<input type=\"checkbox\" onclick=\"ClickToogle(this)\"" + Checked + "></div>";
    RETOUR += "<input class=\"hide\" id=\"actif\" name=\"actif\" value=\"" + Actif + "\" type=\"text\">";
    return RETOUR;
  }

  if (var == "IPAP") {
    if (SPIFFS_Config_Reseau["ap"][0]["ip"].as<String>() == "null") {
      RETOUR = "";
    } else {
      RETOUR = SPIFFS_Config_Reseau["ap"][0]["ip"].as<String>();
    }
    return RETOUR;
  }

  if (var == "MDPAP") {
    if (SPIFFS_Config_Reseau["ap"][0]["passcrypt"].as<String>() == "null") {
      RETOUR = "";
    } else {
      RETOUR = Uncrypt_Pass;
    }
    return RETOUR;
  }



  if (var == "ACTIONS") {
    ScriptConstruct = "";
    ScriptConstruct += "<script>";
    ScriptConstruct += "if (!!window.EventSource) {";
    ScriptConstruct += "var source = new EventSource('/events');";
    ScriptConstruct += "source.addEventListener('open', function(e) {";
    ScriptConstruct += "console.log(\"Events Connected\");";
    ScriptConstruct += "}, false);";
    ScriptConstruct += "source.addEventListener('error', function(e) {";
    ScriptConstruct += "if (e.target.readyState != EventSource.OPEN) {";
    ScriptConstruct += "console.log(\"Events Disconnected\");";
    ScriptConstruct += "}";
    ScriptConstruct += "}, false);";
    ScriptConstruct += "source.addEventListener('message', function(e) {";
    ScriptConstruct += "console.log(\"message\", e.data);";
    ScriptConstruct += "}, false);";
    RETOUR = "";
    JsonArray Actions = SPIFFS_Config_Actions["actions"];
    for (int i = 0; i < Actions.size(); i++) {
      if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "button") {

        if (SPIFFS_Config_Actions["actions"][i]["return"].as<String>() == "true") RETOUR += "<button id=\"" + SPIFFS_Config_Actions["actions"][i]["id"].as<String>() + "\" class=\"Bouton\" onclick=\"Click(this)\" style=\"margin-left:5px;margin-right:5px;text-decoration:none\"> " + SPIFFS_Config_Actions["actions"][i]["nom"].as<String>() + " </button><br>";
        else RETOUR += "<button id=\"" + SPIFFS_Config_Actions["actions"][i]["id"].as<String>() + "\" class=\"Bouton\" onclick=\"Click(this)\" style=\"margin-left:5px;margin-right:5px;text-decoration:none\"> " + SPIFFS_Config_Actions["actions"][i]["nom"].as<String>() + " </button>";


      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "little_button") {

        if (SPIFFS_Config_Actions["actions"][i]["return"].as<String>() == "true") RETOUR += "<button id=\"" + SPIFFS_Config_Actions["actions"][i]["id"].as<String>() + "\" class=\"LittleBouton\" onclick=\"Click(this)\" style=\"margin-left:5px;margin-right:5px;text-decoration:none\"> " + SPIFFS_Config_Actions["actions"][i]["nom"].as<String>() + " </button><br>";
        else RETOUR += "<button id=\"" + SPIFFS_Config_Actions["actions"][i]["id"].as<String>() + "\" class=\"LittleBouton\" onclick=\"Click(this)\" style=\"margin-left:5px;margin-right:5px;text-decoration:none\"> " + SPIFFS_Config_Actions["actions"][i]["nom"].as<String>() + " </button>";


      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "html") {
        String Value = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();
        RETOUR += Value;



      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "title") {
        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
        String Size = SPIFFS_Config_Actions["actions"][i]["size"].as<String>();
        RETOUR += "<b><div style=\"padding-top: 10px; padding-bottom: 10px; font-size: " + Size + "px;\">" + Nom + "</div></b>";
        ScriptConstruct += "source.addEventListener('" + SPIFFS_Config_Actions["actions"][i]["id"].as<String>() + "', function(e) {document.getElementById(\"" + ID + "\").innerHTML = e.data;}, false);";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "text_index") {
        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
        String Size = SPIFFS_Config_Actions["actions"][i]["size"].as<String>();
        RETOUR += "<div style=\"padding-top: 10px; padding-bottom: 10px; font-size: " + Size + "px;\">" + Nom + "</div>";
        ScriptConstruct += "source.addEventListener('" + SPIFFS_Config_Actions["actions"][i]["id"].as<String>() + "', function(e) {document.getElementById(\"" + ID + "\").innerHTML = e.data;}, false);";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "tooltip") {
        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String Content = SPIFFS_Config_Actions["actions"][i]["content"].as<String>();
        String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
        RETOUR += "<div class=\"picto-item\" id=\"" + ID + "\" name=\"" + ID + "\" onclick=\"Click(this)\" aria-label=\"" + Content + "\">" + Nom + "</div>";
        if (SPIFFS_Config_Actions["actions"][i]["return"].as<String>() == "true") RETOUR += "<br>";
        ScriptConstruct += "source.addEventListener('" + SPIFFS_Config_Actions["actions"][i]["id"].as<String>() + "', function(e) {document.getElementById(\"" + ID + "\").innerHTML = e.data;}, false);";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "range") {
        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
        String Min = SPIFFS_Config_Actions["actions"][i]["min"].as<String>();
        String Max = SPIFFS_Config_Actions["actions"][i]["max"].as<String>();
        String Value = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();

        RETOUR += "<div style=\"margin-top: 15px; margin-bottom: 10px;\">";
        RETOUR += "<label for=\"" + ID + "\">" + Nom + ": </label>";
        RETOUR += "<b><output id=\"RV" + ID + "\">" + Value + "</output></b><br>";
        RETOUR += "<input type=\"range\" id=\"" + ID + "\" name=\"" + ID + "\" min=\"" + Min + "\" max=\"" + Max + "\" value=\"" + Value + "\" oninput=\"RV" + ID + ".value=value\" onchange=\"ValidRange(this)\"/>";
        RETOUR += "</div>";

        ScriptConstruct += "source.addEventListener('" + ID + "', function(e) {document.getElementById(\"" + ID + "\").value = e.data; document.getElementById(\"RV" + ID + "\").innerHTML = e.data; }, false);";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "info") {
        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
        String Size = SPIFFS_Config_Actions["actions"][i]["size"].as<String>();
        String Value = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();
        String Unit = SPIFFS_Config_Actions["actions"][i]["unit"].as<String>();
        if (Unit == "%") Unit = "%%";
        RETOUR += "<div class=\"mira-info\">" + Nom + ": <b><span id=\"" + ID + "\">" + Value + "</span></b><span id=\"Unit" + ID + "\"> " + Unit + "</span></div>";
        ScriptConstruct += "source.addEventListener('" + SPIFFS_Config_Actions["actions"][i]["id"].as<String>() + "', function(e) {document.getElementById(\"" + ID + "\").innerHTML = e.data;}, false);";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "infofa") {
        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
        String Size = SPIFFS_Config_Actions["actions"][i]["size"].as<String>();
        String Value = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();
        RETOUR += "<div style=\" margin-top: 10px; \">" + Nom + "</div>";
        RETOUR += "<div style=\"margin-top: 10px; font-size: " + Size + "px;\" ><i id=\"" + ID + "\" class=\"" + Value + "\"></i></div>";
        ScriptConstruct += "source.addEventListener('" + SPIFFS_Config_Actions["actions"][i]["id"].as<String>() + "', function(e) {document.getElementById(\"" + ID + "\").className = e.data;}, false);";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "inputbox") {
        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
        String Value = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();
        String Type = SPIFFS_Config_Actions["actions"][i]["typebox"].as<String>();
        RETOUR += "<b><label>" + Nom + "</label></b><br><input class=\"inputbox\" id=\"" + ID + "\" name=\"" + ID + "\" type=\"" + Type + "\" onBlur=\"InputBoxValidate(this)\"/><br>";
        ScriptConstruct += "source.addEventListener('" + ID + "', function(e) {document.getElementById(\"" + ID + "\").value = e.data;}, false);";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "color") {
        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();
        String R = SPIFFS_Config_Actions["actions"][i]["r"].as<String>();
        String G = SPIFFS_Config_Actions["actions"][i]["g"].as<String>();
        String B = SPIFFS_Config_Actions["actions"][i]["b"].as<String>();

        RETOUR += "<div class=\"FondBlanc_Color\">" + Nom + "<br><input class=\"input_color\" id=\"" + ID + "\" type=\"color\" onchange=\"printColor(this,event)\"/></div>";
        RETOUR += "<script>var rHex = parseInt(" + R + ").toString(16).padStart(2, '0'); var gHex = parseInt(" + G + ").toString(16).padStart(2, '0');var bHex = parseInt(" + B + ").toString(16).padStart(2, '0'); document.getElementById(\"" + ID + "\").value = '#' + rHex + gHex + bHex; </script>";

        ScriptConstruct += "source.addEventListener('" + ID + "', function(e) {document.getElementById(\"" + ID + "\").value = e.data;}, false);";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "eeSeg") {
        RETOUR += "<div style=\"margin-top: 30px;\" class=\"Seg\"></div>";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "eeSegw") {
        RETOUR += "<div style=\"margin-top: 30px;\" class=\"Segw\"></div>";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "toggle") {

        RETOUR += "<div class=\"div_toogle\"><div class=\"div_toogle_name\">" + SPIFFS_Config_Actions["actions"][i]["nom"].as<String>() + "</div>";

        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String Value0 = SPIFFS_Config_Actions["actions"][i]["value0"].as<String>();
        String Value1 = SPIFFS_Config_Actions["actions"][i]["value1"].as<String>();
        String ID0 = SPIFFS_Config_Actions["actions"][i]["id0"].as<String>();
        String ID1 = SPIFFS_Config_Actions["actions"][i]["id1"].as<String>();

        if (SPIFFS_Config_Actions["actions"][i]["state"].as<String>() == Value1) {
          RETOUR += "<label class=\"switch\">";
          RETOUR += "<input type=\"checkbox\" name=\"" + ID0 + "\" id=\"" + ID0 + "\" value=\"" + Value1 + "\" type=\"checkbox\" onclick=\"ClickToogle(this,'" + Value0 + "','" + Value1 + "','" + ID0 + "','" + ID1 + "')\" checked>";
          RETOUR += "<span class=\"slider round\"></span> </label> </div><br>";
        } else {
          RETOUR += "<label class=\"switch\">";
          RETOUR += "<input type=\"checkbox\" name=\"" + ID0 + "\" id=\"" + ID0 + "\" value=\"" + Value1 + "\" type=\"checkbox\" onclick=\"ClickToogle(this,'" + Value0 + "','" + Value1 + "','" + ID0 + "','" + ID1 + "')\">";
          RETOUR += "<span class=\"slider round\"></span> </label> </div><br>";
        }

        ScriptConstruct += "source.addEventListener('" + ID0 + "', function(e) {document.getElementById(\"" + ID0 + "\").checked = false; }, false);";
        ScriptConstruct += "source.addEventListener('" + ID1 + "', function(e) {document.getElementById(\"" + ID0 + "\").checked = true; }, false);";

      } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "list") {

        String NomList = SPIFFS_Config_Actions["actions"][i]["nomliste"].as<String>();
        String Nom = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        String ID = SPIFFS_Config_Actions["actions"][i]["id"].as<String>();

        bool Liste_Existante = false;
        for (int k = 0; k < ID_Memoire_Liste; k++) {
          if (Memoire_Liste[k] == NomList) {
            Liste_Existante = true;

            break;
          }
        }

        String Statut;
        for (int k = 0; k < NB_Listes; k++) {
          if (Listes[k] == NomList) {
            Statut = Listes_Value[k];
            break;
          }
        }


        if (!Liste_Existante) {
          RETOUR += "<label for=\"" + NomList + "\">" + NomList + "</label><br>";
          RETOUR += "<select class=\"box\" name=\"" + NomList + "\" id=\"" + NomList + "\" onchange=\"Liste(this)\">";
          RETOUR += "<option id=\"" + ID + "\" value=\"" + ID + "\">" + Nom + "</option>";
          ScriptConstruct += "source.addEventListener('" + ID + "', function(e) {document.getElementById(\"" + NomList + "\").value = " + ID + ";}, false);";

          for (int j = i + 1; j < Actions.size(); j++) {
            if (SPIFFS_Config_Actions["actions"][j]["type"].as<String>() == "list" && SPIFFS_Config_Actions["actions"][j]["nomliste"].as<String>() == NomList) {
              Nom = SPIFFS_Config_Actions["actions"][j]["nom"].as<String>();
              ID = SPIFFS_Config_Actions["actions"][j]["id"].as<String>();
              if (ID == Statut) {
                RETOUR += "<option id=\"" + ID + "\" value=\"" + ID + "\" selected>" + Nom + "</option>";
              } else {
                RETOUR += "<option id=\"" + ID + "\" value=\"" + ID + "\">" + Nom + "</option>";
              }

              ScriptConstruct += "source.addEventListener('" + ID + "', function(e) {document.getElementById(\"" + NomList + "\").value = " + ID + ";}, false);";
            }
          }

          Memoire_Liste[ID_Memoire_Liste] = NomList;
          ID_Memoire_Liste++;
          RETOUR += "</select><br>";
        }
      }
      if (i == Actions.size() - 1) {
        ScriptConstruct += "}</script>";
        RETOUR += ScriptConstruct;
      }
    }
  }
  ID_Memoire_Liste = 0;
  return RETOUR;
}





void MiraPanelServer::Init() {

  if (_Debug) Serial.println(F("[MIRA] Initialisation du serveur"));
  delay(50);






  cipher->setKey(key);



  SPIFFS_Initialisation();
  if (!SPIFFS.exists("/reseau.json")) SPIFFS_CreationConfigReseau();
  if (!SPIFFS.exists("/params.json")) SPIFFS_CreationConfigParams();
  if (!SPIFFS.exists("/miralvgl.json")) SPIFFS_Creation_lvgl();
  SPIFFS_ChargementConfigurationReseau();
  SPIFFS_ChargementConfigurationParams();
  SPIFFS_ChargementConfigurationLvgl();

  JsonArray Params = SPIFFS_Config_Actions.createNestedArray("actions");
  JsonArray Module = SPIFFS_Config_Module.createNestedArray("module");

  if (_Scan == true) ScanWiFi();

  if (SPIFFS_Config_Reseau["ap"][0]["actif"].as<String>() == "0") {
    if (SPIFFS_Config_Reseau["ap"][0]["passcrypt"].as<String>() == "null") {
      SPIFFS_ResetReseau();
      SPIFFS_CreationConfigReseau();
      delay(500);
      ESP.restart();
    } else if (SPIFFS_Config_Reseau["ap"][0]["passcrypt"].as<String>() == "") {
      ModeAP = true;
      ServeurDNS();
    } else {
      if (initWiFi()) {
        ModeAP = false;
        ServeurStation();


        ArduinoOTA
          .onStart([]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH)
              type = "sketch";
            else
              type = "filesystem";
          })
          .onEnd([]() {

          })
          .onProgress([](unsigned int progress, unsigned int total) {

          })
          .onError([](ota_error_t error) {

          });

        ArduinoOTA.begin();


      } else {
        if (initPA()) {
          BesoinCo = true;
          ModeAP = true;
          ServeurStation();
        }
      }
    }

  } else {
    if (initPA()) {
      ModeAP = true;
      ServeurStation();
    }
  }
}


void MiraPanelServer::AsyncHTTP(String URL) {
  if (_Debug) Serial.println(F("[MIRA] Envoi de la requête HTTP asyncrone"));
  int Length_URL = URL.length();
  Length_URL++;
  char Buf[Length_URL];
  URL.toCharArray(Buf, Length_URL);
  miraAsyncHTTPGet(Buf);
}

void MiraPanelServer::JeedomInit() {
  JEEDOM_mira = GetParamBool("Jeedom");
  JEEDOM_Refresh = GetParamText("Jeedom_Refresh");
  JEEDOM_Init = GetParamText("Jeedom_Init_Cmd");
  JEEDOM_Key_mira = GetParamText("Jeedom_Key_mira");
  JEEDOM_Key = GetParamText("Jeedom_Key");
  JEEDOM_IP = GetParamText("Jeedom_IP");
  if (JEEDOM_mira && JEEDOM_Init != "" && JEEDOM_Key_mira != "" && JEEDOM_IP != "" && JEEDOM_Refresh != "" && JEEDOM_Init != "null" && JEEDOM_Key_mira != "null" && JEEDOM_IP != "null" && JEEDOM_Refresh != "null") {

    if (JEEDOM_mira) {
      String URL = JEEDOM_IP + "/core/api/jeeApi.php?plugin=mira&type=event&apikey=" + JEEDOM_Key_mira + "&id=" + JEEDOM_Init;
      if (_Debug) Serial.println(F("[MIRA] Envoi de l'Init Jeedom"));
      int Length_URL = URL.length();
      Length_URL++;
      char Buf[Length_URL];
      URL.toCharArray(Buf, Length_URL);
      miraAsyncHTTPGet(Buf);
    }
  }
}

void MiraPanelServer::JeedomRefresh() {
  JEEDOM_mira = GetParamBool("Jeedom");
  JEEDOM_Refresh = GetParamText("Jeedom_Refresh");
  JEEDOM_Init = GetParamText("Jeedom_Init_Cmd");
  JEEDOM_Key_mira = GetParamText("Jeedom_Key_mira");
  JEEDOM_Key = GetParamText("Jeedom_Key");
  JEEDOM_IP = GetParamText("Jeedom_IP");
  if (JEEDOM_mira && JEEDOM_Refresh != "" && JEEDOM_Key != "" && JEEDOM_IP != "" && JEEDOM_Refresh != "null" && JEEDOM_Key != "null" && JEEDOM_IP != "null") {
    String URL = JEEDOM_IP + "/core/api/jeeApi.php?plugin=mira&type=event&apikey=" + JEEDOM_Key_mira + "&id=" + JEEDOM_Refresh;
    int Length_URL = URL.length();
    Length_URL++;
    char Buf[Length_URL];
    URL.toCharArray(Buf, Length_URL);
    miraAsyncHTTPGet(Buf);
    if (_Debug) Serial.println(F("[MIRA] Envoi du refresh Jeedom"));
  }
}

/*void MiraPanelServer::JeedomTimerRefresh() {
  JEEDOM_Moment_Present = millis();
  if (JEEDOM_Moment_Present - JEEDOM_Moment_Memorise > 100) {
    JeedomRefresh();
    JEEDOM_Moment_Memorise = 0;
    JEEDOM_Moment_Present = 0;
  }
}*/
void MiraPanelServer::IndexResetEvent() {
  //Action = -1;
}

int MiraPanelServer::IndexGetPassiveToggleEvent() {
  if (_passiveToggleHead == _passiveToggleTail) return -1;
  const int pending = _passiveToggleEvents[_passiveToggleHead];
  _passiveToggleHead =
      uint8_t((_passiveToggleHead + 1) % PASSIVE_TOGGLE_QUEUE_SIZE);
  return pending;
}

int MiraPanelServer::ParamGetEvent() {
  int memoireparamEvent = paramEvent;
  paramEvent = -1;
  return memoireparamEvent;
}

int MiraPanelServer::IndexGetEvent() {
  if (DNS == true) {
    dnsServer.processNextRequest();
    RSTHandler();

  } else {


    if (millis() - MomentPrecedent_Surv > 300000) {
      MomentPrecedent_Surv = millis();
      if ((ModeAP == false || BesoinCo == true) && TentativesReco == 0) {

        if (WiFi.status() != WL_CONNECTED) {
          if (_Debug == true) Serial.println(F("[MIRA] Perte de connexion Wi-Fi..."));
          if (_Scan == true) ScanWiFi();

          if (_Debug == true) Serial.println(F("[MIRA] Tentative de reconnexion..."));
          if (ReInitWiFi()) {
            TentativesReco = 0;
            ModeAP = false;
            ConnexionPerdue = false;
            BesoinCo = false;
            WiFi.mode(WIFI_STA);
            if (_Debug == true) Serial.println(F("[MIRA] Connexion retrouvée!"));
          } else {
            if (initPA()) {

              if (_Debug == true) Serial.println(F("[MIRA] Impossible de se reconnecter! Activation du point d'accès..."));
              ModeAP = true;
              ConnexionPerdue = true;
            }
          }
        }

      } else if (ConnexionPerdue == true) {
        TentativesReco++;
        if (_Debug == true) Serial.println("[MIRA] " + String(TentativesReco));
        if (TentativesReco >= 2) {
          if (_Debug == true) Serial.println(F("[MIRA] Lancement de la tentative de reconnexion..."));
          TentativesReco = 0;
          ModeAP = false;
        }
      }
    }
    ArduinoOTA.handle();

    //Jeedom mira
    if (!Init_Jeedom) {
      JEEDOM_mira = GetParamBool("Jeedom");
      if (!Init_Jeedom && JEEDOM_mira) {
        ActionGet = false;
        JeedomInit();
        Init_Jeedom = true;
        //JEEDOM_Moment_Memorise = millis();
      }

      if (Action != -1 && AUTO_REFRESH && !JEEDOM_mira) {  //envoi url Generic
        GenericCmd(Action);
      }
    } else {
      JEEDOM_mira = GetParamBool("Jeedom");
      if (Action != ACTION_UPDATE && Action != -1 && JEEDOM_mira && AUTO_REFRESH) {
        JeedomCmd(Action);
      }
      if (JEEDOM_mira) {
        SendQueud();
      }
      if (ReponseAsync != "null" && JEEDOM_mira) {
        for (int i = 0; i < 5; i++) {
          if (eQueud[i].cmd == ReponseAsync) {
            if (_Debug) Serial.print(F("[MIRA][QUEUE] Retrait : "));
            if (_Debug) Serial.println(eQueud[i].cmd);
            eQueud[i].id = -1;
            eQueud[i].cmd = "";
            eQueud[i].value = "";
            eQueud[i].send = false;
            eQueud[i].test = 0;
            ReorderQueued();
          }
        }



        ReponseAsync = "null";
      }
    }
    RSTHandler();

    //MAJ
    int Memoire_Action_Event = -1;
    if (Action == ACTION_UPDATE) {
      if (!update_module(UpdateURL)) {
        if (_Debug) Serial.print(F("[MIRA] Il y a eu un problème lors de la MAJ..."));
        events.send("Mise à jour impossible, le serveur a retourné une mauvaise réponse...", "divmessage", millis());
      }
      Action = -1;


    } else {
      Memoire_Action_Event = Action;
      Action = -1;
    }

    return Memoire_Action_Event;
  }
}


void MiraPanelServer::SendQueud() {
  if (eQueud[0].id != -1 && !eQueud[0].send) {

    if (eQueud[0].cmd != "" && eQueud[0].value == "") {

      String URL = JEEDOM_IP + "/core/api/jeeApi.php?plugin=mira&type=event&apikey=" + JEEDOM_Key_mira + "&id=" + eQueud[0].cmd;
      if (_Debug) Serial.print(F("[MIRA] Envoi de l'action Jeedom: "));
      if (_Debug) Serial.println(eQueud[0].cmd);
      //HTTPGet(URL);
      int Length_URL = URL.length();
      Length_URL++;
      char Buf[Length_URL];
      URL.toCharArray(Buf, Length_URL);
      miraAsyncHTTPGet(Buf);




    } else if (eQueud[0].cmd != "" && eQueud[0].value != "") {
      String URL = JEEDOM_IP + "/core/api/jeeApi.php?plugin=mira&type=event&apikey=" + JEEDOM_Key_mira + "&id=" + eQueud[0].cmd + "&value=" + eQueud[0].value;
      if (_Debug) Serial.print(F("[MIRA] Envoi de l'info Jeedom: "));
      if (_Debug) Serial.print(eQueud[0].cmd);
      if (_Debug) Serial.print(F(" : "));
      if (_Debug) Serial.println(eQueud[0].value);
      //HTTPGet(URL);
      int Length_URL = URL.length();
      Length_URL++;
      char Buf[Length_URL];
      URL.toCharArray(Buf, Length_URL);
      miraAsyncHTTPGet(Buf);
    }
    eQueud[0].send = true;
    eQueud[0].test = 1;
  }

  if (Queued_Moment_Memorise != 0 && millis() - Queued_Moment_Memorise > 3000) {
    if (eQueud[0].send) eQueud[0].send = false;
    if (eQueud[0].test < 3) {
      Queued_Moment_Memorise = millis();
      eQueud[0].test++;
    } else {
      Queued_Moment_Memorise = 0;
      eQueud[0].id = -1;
      eQueud[0].cmd = "";
      eQueud[0].value = "";
      eQueud[0].send = false;
      eQueud[0].test = 0;
      ReorderQueued();
    }
  }
}

void MiraPanelServer::ReorderQueued() {

  for (int i = 0; i < 5; i++) {
    bool Trouve = false;
    if (eQueud[i].id == -1) {
      for (int j = i; j < 5 && !Trouve; j++) {
        if (eQueud[j].id != -1) {
          eQueud[i].id = eQueud[j].id;
          eQueud[i].cmd = eQueud[j].cmd;
          eQueud[i].value = eQueud[j].value;
          eQueud[i].send = eQueud[j].send;
          eQueud[i].test = eQueud[j].test;
          eQueud[j].id = -1;
          eQueud[j].cmd = "";
          eQueud[j].value = "";
          eQueud[j].send = false;
          eQueud[j].test = 0;
          Trouve = true;
        }
      }
    }
    if (_Debug) Serial.println(eQueud[i].id);
  }
}


void MiraPanelServer::JeedomCmd(int ID) {
  if (Resend) {
    if (_Debug) Serial.print(F("[MIRA][QUEUE] Ajout : "));

    JsonArray Actions = SPIFFS_Config_Actions["actions"];
    String Jeedom_ID = "";
    String Jeedom_Value = "";
    for (int i = 0; i < Actions.size(); i++) {
      if (SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() == String(ID)) {
        Jeedom_ID = SPIFFS_Config_Actions["actions"][i]["jeedom0"].as<String>();
      } else if (SPIFFS_Config_Actions["actions"][i]["id1"].as<String>() == String(ID)) {
        Jeedom_ID = SPIFFS_Config_Actions["actions"][i]["jeedom1"].as<String>();
      } else if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
        Jeedom_ID = SPIFFS_Config_Actions["actions"][i]["jeedom"].as<String>();
        if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "range"
            || SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "info") {
          Jeedom_Value = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();
        } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "list") {
          Jeedom_Value = String(ID);
        } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "color") {
          int R = (SPIFFS_Config_Actions["actions"][i]["r"].as<String>()).toInt();
          int G = (SPIFFS_Config_Actions["actions"][i]["g"].as<String>()).toInt();
          int B = (SPIFFS_Config_Actions["actions"][i]["b"].as<String>()).toInt();
          char hexArray[6] = { 0 };
          sprintf(hexArray, "%02X%02X%02X", R, G, B);
          Jeedom_Value = "%23";
          Jeedom_Value += hexArray;
        }
      }
      if (Jeedom_ID != "") break;
    }

    if (Jeedom_ID != "") {

      for (int i = 0; i < 5; i++) {
        if (eQueud[i].id == -1) {
          eQueud[i].id = ID;
          eQueud[i].cmd = Jeedom_ID;
          eQueud[i].value = Jeedom_Value;
          eQueud[i].send = false;
          eQueud[i].test = 0;

          if (_Debug) Serial.println(eQueud[i].cmd);
          Queued_Moment_Memorise = millis();
          break;
        }
      }
    }



    /*if (Jeedom_ID != "" && Jeedom_Value == "") {

        String URL = JEEDOM_IP + "/core/api/jeeApi.php?plugin=mira&type=event&apikey=" + JEEDOM_Key_mira + "&id=" + Jeedom_ID;
        if (_Debug) Serial.print(F("[MIRA] Envoi de l'action Jeedom: "));
        if (_Debug) Serial.println(Jeedom_ID);
        //HTTPGet(URL);
        int Length_URL = URL.length();
        Length_URL++;
        char Buf[Length_URL];
        URL.toCharArray(Buf, Length_URL);
        miraAsyncHTTPGet(Buf);



        //break;
      } else if (Jeedom_ID != "" && Jeedom_Value != "") {
        String URL = JEEDOM_IP + "/core/api/jeeApi.php?plugin=mira&type=event&apikey=" + JEEDOM_Key_mira + "&id=" + Jeedom_ID + "&value=" + Jeedom_Value;
        if (_Debug) Serial.print(F("[MIRA] Envoi de l'info Jeedom: "));
        if (_Debug) Serial.print(Jeedom_ID);
        if (_Debug) Serial.print(F(" : "));
        if (_Debug) Serial.println(Jeedom_Value);
        //HTTPGet(URL);
        int Length_URL = URL.length();
        Length_URL++;
        char Buf[Length_URL];
        URL.toCharArray(Buf, Length_URL);
        miraAsyncHTTPGet(Buf);


        //break;
      }*/
  }
}

void MiraPanelServer::GenericCmd(int ID) {
  if (Resend) {
    JsonArray Actions = SPIFFS_Config_Actions["actions"];
    String GenericValue = "";
    String GenericName = "";
    for (int i = 0; i < Actions.size(); i++) {
      if (SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() == String(ID)) {
        GenericValue = SPIFFS_Config_Actions["actions"][i]["value0"].as<String>();
        GenericName = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        break;
      } else if (SPIFFS_Config_Actions["actions"][i]["id1"].as<String>() == String(ID)) {
        GenericValue = SPIFFS_Config_Actions["actions"][i]["value1"].as<String>();
        GenericName = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        break;
      } else if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == String(ID)) {
        GenericValue = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();
        GenericName = SPIFFS_Config_Actions["actions"][i]["nom"].as<String>();
        if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "range"
            || SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "info") {
          GenericValue = SPIFFS_Config_Actions["actions"][i]["value"].as<String>();
        } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "list") {
          GenericValue = String(ID);
        } else if (SPIFFS_Config_Actions["actions"][i]["type"].as<String>() == "color") {
          int R = (SPIFFS_Config_Actions["actions"][i]["r"].as<String>()).toInt();
          int G = (SPIFFS_Config_Actions["actions"][i]["g"].as<String>()).toInt();
          int B = (SPIFFS_Config_Actions["actions"][i]["b"].as<String>()).toInt();
          char hexArray[6] = { 0 };
          sprintf(hexArray, "%02X%02X%02X", R, G, B);
          GenericValue = "%23";
          GenericValue += hexArray;
        }
        break;
      }
    }

    JsonArray Json_Array = mira_Json_LVGL["lvgl"];
    for (int i = 0; i < Json_Array.size(); i++) {
      if (mira_Json_LVGL["lvgl"][i]["name"].as<String>() == GenericName) {
        String URL = mira_Json_LVGL["lvgl"][i]["url"].as<String>();
        if (GenericValue != "") URL.replace("#value#", GenericValue);

        int Length_URL = URL.length();
        Length_URL++;
        char Buf[Length_URL];
        URL.toCharArray(Buf, Length_URL);
        miraAsyncHTTPGet(Buf);
        if (_Debug) Serial.print(F("[MIRA] Envoi de l'info Generic: "));


        break;
      }
    }
  }
}
