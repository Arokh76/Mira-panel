#define JSON_CONFIG_Reseau "/reseau.json"
#define JSON_CONFIG_Params "/params.json"
#define JSON_CONFIG_lvgl "/miralvgl.json"

DynamicJsonDocument SPIFFS_Config_Reseau(4096);
DynamicJsonDocument SPIFFS_Config_Params(32768);
DynamicJsonDocument SPIFFS_Config_Module(512);
DynamicJsonDocument SPIFFS_Config_Actions(16384);
DynamicJsonDocument mira_Json_LVGL(16384);




/*DynamicJsonDocument SPIFFS_Config_Reseau(16384);
DynamicJsonDocument SPIFFS_Config_Params(32768);
DynamicJsonDocument SPIFFS_Config_Module(1024);
DynamicJsonDocument SPIFFS_Config_Actions(16384);
DynamicJsonDocument mira_Json_LVGL(16384);*/

String JEEDOM_Refresh = "";
String JEEDOM_Key = "";
String JEEDOM_Key_mira = "";
String JEEDOM_IP = "";
String JEEDOM_Init = "";
String Uncrypt_Pass="";

bool Init_Jeedom = false;
bool Resend = true;


String SPIFFS_ID = "null";
String SPIFFS_SSID = "null";
String SPIFFS_Pass = "null";
String SPIFFS_IP = "null";
String SPIFFS_IP_Mode = "auto";
String SPIFFS_Mask = "";
String SPIFFS_Gateway = "";
String SPIFFS_DNS1 = "";
String SPIFFS_DNS2 = "";

String SPIFFS_ADD_Param = "";
String SPIFFS_ADD_Value = "";
String SPIFFS_ADD_Type = "";

String LVGL_Type = "";
String LVGL_Name = "";
String LVGL_Emplacement = "";
String LVGL_Min = "";
String LVGL_Max = "";
String LVGL_Value = "";
String LVGL_Unit = "";
String LVGL_Style = "standard";
String LVGL_Layout = "auto";
String LVGL_LabelMode = "auto";
String LVGL_Icon = "auto";
String LVGL_DisplayName = "";
int LVGL_LayoutX = -1;
int LVGL_LayoutY = -1;
int LVGL_LayoutW = -1;
int LVGL_LayoutH = -1;

int NB_List_Param = 0;
int NB_List_lvgl = 0;

int paramEvent = -1;

bool _Debug = false;
bool _LVGL = false;
bool _Scan = true;

Cipher* cipher = new Cipher();
char* key = "C1W0CleCipherMMn";

String GetHttp(String URL) {
  if (URL != "") {
    HTTPClient http;
    http.begin(URL);
    http.addHeader("Content-Type", "text/plain");
    int httpResponseCode = http.GET();
    if (httpResponseCode > 0) {
      String response = http.getString();
      return response;
    } else {
      if (_Debug == true) Serial.print(F("[MIRA][FS] Erreur GET HTTP: "));
      if (_Debug == true) Serial.println(httpResponseCode);
      return "Error";
    }
    http.end();
    delay(10);
  }
}

void OrderIdJeedom(String ID) {
   String URL = JEEDOM_IP + "/core/api/jeeApi.php?plugin=mira&type=event&apikey=" + JEEDOM_Key_mira + "&id=" + ID + "&value=json";
  String Get = GetHttp(URL);
  if (_Debug == true) Serial.print(F("[MIRA][FS] Récupération du fichier JSON de Jeedom: "));
  if (_Debug == true) Serial.println(Get);
  deserializeJson(SPIFFS_Config_Actions, Get);

}

void SetJeedomId(String ID, String Value) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id"].as<String>() == ID || SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() == ID || SPIFFS_Config_Actions["actions"][i]["id1"].as<String>() == ID) {
      SPIFFS_Config_Actions["actions"][i]["jeedom"] = Value;
      break;
    }
  }
}

void SetJeedomToggleId(String ID, String Value0, String Value1) {
  JsonArray Actions = SPIFFS_Config_Actions["actions"];
  for (int i = 0; i < Actions.size(); i++) {
    if (SPIFFS_Config_Actions["actions"][i]["id0"].as<String>() == ID || SPIFFS_Config_Actions["actions"][i]["id1"].as<String>() == ID) {
      SPIFFS_Config_Actions["actions"][i]["jeedom0"] = Value0;
      SPIFFS_Config_Actions["actions"][i]["jeedom1"] = Value1;
      break;
    }
  }
}
void RemoveListLVGL() {
  JsonArray Json_Array = mira_Json_LVGL["lvgl"];
  int taille = Json_Array.size();
  for (int i = 0; i < taille; i++) {
    String nom = mira_Json_LVGL["lvgl"][0]["name"];
    Json_Array.remove(0);
  }
  if (_Debug == true) serializeJsonPretty(mira_Json_LVGL, Serial);
  NB_List_lvgl = 0;
  paramEvent = MIRA_REMOVE_LIST_LVGL;
}

void AddListLVGL() {
  JsonArray Json_Array = mira_Json_LVGL["lvgl"];
  JsonObject lvgl_array = Json_Array.createNestedObject();
  if (_Debug == true) Serial.print("[MIRA][FS] Ajout du LVGL: ");
  if (_Debug == true) Serial.println(LVGL_Name);
  lvgl_array["type"] = LVGL_Type;
  lvgl_array["name"] = LVGL_Name;
  lvgl_array["emplacement"] = LVGL_Emplacement;
  lvgl_array["min"] = LVGL_Min;
  lvgl_array["max"] = LVGL_Max;
  lvgl_array["value"] = LVGL_Value;
  lvgl_array["unit"] = LVGL_Unit;
  lvgl_array["style"] = LVGL_Style.length() ? LVGL_Style : "standard";
  lvgl_array["layout"] = LVGL_Layout.length() ? LVGL_Layout : "auto";
  lvgl_array["labelMode"] = LVGL_LabelMode;
  lvgl_array["icon"] = LVGL_Icon;
  lvgl_array["displayName"] = LVGL_DisplayName;
  if (LVGL_Layout == "free" && LVGL_LayoutX >= 0 && LVGL_LayoutY >= 0 && LVGL_LayoutW > 0 && LVGL_LayoutH > 0) {
    lvgl_array["lx"] = LVGL_LayoutX;
    lvgl_array["ly"] = LVGL_LayoutY;
    lvgl_array["lw"] = LVGL_LayoutW;
    lvgl_array["lh"] = LVGL_LayoutH;
  }
  if (LVGL_Type != "info" && LVGL_Type != "datetime") lvgl_array["url"] = "";
  if (_Debug == true) {
    serializeJsonPretty(mira_Json_LVGL, Serial);
    if (mira_Json_LVGL.overflowed()) Serial.println("[MIRA][FS] ERREUR: document LVGL sature");
  }
  paramEvent = MIRA_ADD_LIST_LVGL;
  NB_List_lvgl++;
}

void SaveListLVGL() {

  if (NB_List_lvgl > 0) {
    File configFile = SPIFFS.open(JSON_CONFIG_lvgl, "w");

    if (!configFile) {
      if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier lvgl pour écriture...");
    }

    if (serializeJson(mira_Json_LVGL, configFile) == 0) {
      if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'écrire le fichier lvgl...");
    }
    configFile.close();
    if (_Debug == true) serializeJsonPretty(mira_Json_LVGL, Serial);
    if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement terminé!");
    paramEvent = MIRA_SAVE_LIST_LVGL;
    NB_List_lvgl = 0;
    delay(200);
  }
}

// Initialisation SPIFFS
void SPIFFS_Initialisation() {
  if (!SPIFFS.begin(true)) {
    if (_Debug == true) Serial.println("[MIRA][FS] SPIFFS - Erreur");
  }
  if (_Debug == true) Serial.println("[MIRA][FS] SPIFFS - Initialisation terminée");
}

void SPIFFS_Format(bool REB = false) {
  SPIFFS.format();
  if (_Debug == true) Serial.println(F("[MIRA][FS] Formatage terminé!"));
  paramEvent = MIRA_FORMAT;
  delay(500);
  if (REB) ESP.restart();
}

void SPIFFS_ResetParams() {

  SPIFFS.remove(JSON_CONFIG_Params);
  SPIFFS.remove(JSON_CONFIG_lvgl);
  if (_Debug == true) Serial.println("[MIRA][FS] Réinitialisation de la configuration terminée!");
}

String SPIFFS_Crypt(String Value) {
  String Crypt = cipher->encryptString(Value);
  return Crypt;
}

String SPIFFS_Uncrypt(String Value) {
  String Decrypt = cipher->decryptString(Value);
  String temp = Decrypt.substring(Decrypt.length() - 1, Decrypt.length());
  // Pas de trace ici : ne jamais journaliser le contenu dechiffre.
  if (temp == NULL)return Decrypt.substring(0, Decrypt.length() - 1);
  
  //if (Decrypt.length() < 16) return Decrypt.substring(0, Decrypt.length() - 1);
  else return Decrypt;
  
}

void SPIFFS_ResetReseau() {
  SPIFFS.remove(JSON_CONFIG_Reseau);
  if (_Debug == true) Serial.println("[MIRA][FS] Réinitialisation du réseau terminée!");
}

void SPIFFS_ResetAll() {
  SPIFFS.remove(JSON_CONFIG_Params);
  SPIFFS.remove(JSON_CONFIG_Reseau);
  if (_Debug == true) Serial.println("[MIRA][FS] Réinitialisation terminée!");
}


void SPIFFS_AddParam() {
  JsonArray Params = SPIFFS_Config_Params["params"];
  if (Params.size() == 0) {
    if (SPIFFS_ADD_Type == "crypted-text") {
      SPIFFS_ADD_Value = cipher->encryptString(SPIFFS_ADD_Value);
    }
    JsonObject Param = Params.createNestedObject();
    Param["param"] = SPIFFS_ADD_Param;
    Param["value"] = SPIFFS_ADD_Value;
    Param["type"] = SPIFFS_ADD_Type;
    File configFile = SPIFFS.open(JSON_CONFIG_Params, "w");

    if (!configFile) {
      if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture...");
    }

    if (serializeJson(SPIFFS_Config_Params, configFile) == 0) {
      if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'écrire le fichier de configuration...");
    }
    configFile.close();
    if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Params, Serial);
    if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement terminé!");
    delay(200);
  } else {
    for (int i = 0; i < Params.size(); i++) {

      if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == SPIFFS_ADD_Param) {
        break;
      }
      if (i == Params.size() - 1) {
        JsonObject Param = Params.createNestedObject();
        if (SPIFFS_ADD_Type == "crypted-text") {
          SPIFFS_ADD_Value = cipher->encryptString(SPIFFS_ADD_Value);
        }
        Param["param"] = SPIFFS_ADD_Param;
        Param["value"] = SPIFFS_ADD_Value;
        Param["type"] = SPIFFS_ADD_Type;
        File configFile = SPIFFS.open(JSON_CONFIG_Params, "w");

        if (!configFile) {
          if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture...");
        }

        if (serializeJson(SPIFFS_Config_Params, configFile) == 0) {
          if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'écrire le fichier de configuration...");
        }
        configFile.close();
        if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Params, Serial);
        if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement terminé!");
        //delay(200);
        break;
      }
      //delay(10);
    }
  }
}




void SPIFFS_RemoveParam() {
  JsonArray Params = SPIFFS_Config_Params["params"];
  for (int i = 0; i < Params.size(); i++) {

    if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == SPIFFS_ADD_Param) {
      Params.remove(i);
      File configFile = SPIFFS.open(JSON_CONFIG_Params, "w");

      if (!configFile) {
        if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture...");
      }

      if (serializeJson(SPIFFS_Config_Params, configFile) == 0) {
        if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'écrire le fichier de configuration...");
      }
      configFile.close();
      if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Params, Serial);
      if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement terminé!");
      delay(200);
      break;
    }
    if (i == Params.size() - 1) {
      if (_Debug == true) Serial.println("[MIRA][FS] Aucun paramètre supprimé.");
      break;
    }
    delay(10);
  }
}

void SPIFFS_UpdateParam() {
  JsonArray Params = SPIFFS_Config_Params["params"];
  for (int i = 0; i < Params.size(); i++) {
    if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == SPIFFS_ADD_Param) {
      if (SPIFFS_Config_Params["params"][i]["value"].as<String>() != SPIFFS_ADD_Value) {
        if (SPIFFS_Config_Params["params"][i]["type"].as<String>() == "crypted-text") {
          SPIFFS_ADD_Value = cipher->encryptString(SPIFFS_ADD_Value);
        }
        SPIFFS_Config_Params["params"][i]["value"] = SPIFFS_ADD_Value;

        File configFile = SPIFFS.open(JSON_CONFIG_Params, "w");
        if (!configFile) {
          if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture...");
        }

        if (serializeJson(SPIFFS_Config_Params, configFile) == 0) {
          if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'écrire le fichier de configuration...");
        }
        configFile.close();
        if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Params, Serial);
        if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement terminé!");
        delay(200);
        break;
      }
    }
    delay(10);
  }
}

String SPIFFS_ReturnAllParams() {
  String Retour_Json = "";
  serializeJsonPretty(SPIFFS_Config_Params, Retour_Json);
  return Retour_Json;
}

void SPIFFS_AddListParam() {
  JsonArray Params = SPIFFS_Config_Params["params"];
  if (Params.size() == 0) {

    NB_List_Param++;
    if (_Debug == true) Serial.print("[MIRA][FS] Ajout du paramètre: ");
    if (_Debug == true) Serial.println(SPIFFS_ADD_Param);
    JsonObject Param = Params.createNestedObject();
    if (SPIFFS_ADD_Type == "crypted-text") {
      SPIFFS_ADD_Value = cipher->encryptString(SPIFFS_ADD_Value);
    }
    Param["param"] = SPIFFS_ADD_Param;
    Param["value"] = SPIFFS_ADD_Value;
    Param["type"] = SPIFFS_ADD_Type;
  } else {
    for (int i = 0; i < Params.size(); i++) {

      if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == SPIFFS_ADD_Param) {
        break;
      }



      if (i == Params.size() - 1) {
        NB_List_Param++;
        if (_Debug == true) Serial.print("[MIRA][FS] Ajout du paramètre: ");
        if (_Debug == true) Serial.println(SPIFFS_ADD_Param);
        JsonObject Param = Params.createNestedObject();
        if (SPIFFS_ADD_Type == "crypted-text") {
          SPIFFS_ADD_Value = cipher->encryptString(SPIFFS_ADD_Value);
        }
        Param["param"] = SPIFFS_ADD_Param;
        Param["value"] = SPIFFS_ADD_Value;
        Param["type"] = SPIFFS_ADD_Type;
      }
    }
  }
}


void SPIFFS_UpdateListParam() {
  JsonArray Params = SPIFFS_Config_Params["params"];
  for (int i = 0; i < Params.size(); i++) {
    if (SPIFFS_Config_Params["params"][i]["param"].as<String>() == SPIFFS_ADD_Param) {
      if (SPIFFS_Config_Params["params"][i]["value"].as<String>() != SPIFFS_ADD_Value) {
        if (SPIFFS_Config_Params["params"][i]["type"].as<String>() == "crypted-text") {
          SPIFFS_ADD_Value = cipher->encryptString(SPIFFS_ADD_Value);
        }
        SPIFFS_Config_Params["params"][i]["value"] = SPIFFS_ADD_Value;
        NB_List_Param++;
        break;
      }
    }
  }
}

void SPIFFS_SaveParam_List() {
  if (NB_List_Param > 0) {
    File configFile = SPIFFS.open(JSON_CONFIG_Params, "w");
    if (!configFile) {
      if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture...");
    }

    if (serializeJson(SPIFFS_Config_Params, configFile) == 0) {
      if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'écrire le fichier de configuration...");
    }
    configFile.close();
    if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Params, Serial);
    if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement terminé!");
    delay(200);
    NB_List_Param = 0;
  }
}




void SPIFFS_ChargementConfigurationReseau() {
  //Lecture du fichier Reseau
  if (SPIFFS.exists(JSON_CONFIG_Reseau)) {
    if (_Debug == true) Serial.println("[MIRA][FS] Lecture du fichier de configuration réseau");
    File configFile = SPIFFS.open(JSON_CONFIG_Reseau, "r");
    if (configFile) {
      DeserializationError error = deserializeJson(SPIFFS_Config_Reseau, configFile);
      if (error)
        if (_Debug == true) Serial.println(F("[MIRA][FS] Lecture impossible"));
      configFile.close();
      JsonArray reseaux = SPIFFS_Config_Reseau["reseau"];
      int DernierReseau = reseaux.size() - 1;
      SPIFFS_SSID = SPIFFS_Config_Reseau["reseau"][DernierReseau]["ssid"].as<String>();
      SPIFFS_Pass = SPIFFS_Uncrypt(SPIFFS_Config_Reseau["reseau"][DernierReseau]["pass"].as<String>());
      SPIFFS_IP = SPIFFS_Config_Reseau["reseau"][DernierReseau]["ip"].as<String>();
       SPIFFS_IP_Mode = SPIFFS_Config_Reseau["reseau"][DernierReseau]["ip_mode"] | "";
       if (SPIFFS_IP_Mode.length() == 0) SPIFFS_IP_Mode = (SPIFFS_IP == "Auto" || SPIFFS_IP == "null" || SPIFFS_IP == "") ? "auto" : "fixed_auto";
       SPIFFS_Mask = SPIFFS_Config_Reseau["reseau"][DernierReseau]["mask"] | "";
       SPIFFS_Gateway = SPIFFS_Config_Reseau["reseau"][DernierReseau]["gateway"] | "";
       SPIFFS_DNS1 = SPIFFS_Config_Reseau["reseau"][DernierReseau]["dns1"] | "";
       SPIFFS_DNS2 = SPIFFS_Config_Reseau["reseau"][DernierReseau]["dns2"] | "";
      Uncrypt_Pass = SPIFFS_Uncrypt(SPIFFS_Config_Reseau["ap"][0]["passcrypt"].as<String>());      
      
    }
  }
}

void SPIFFS_ChargementConfigurationParams() {
  //Lecture du fichier Params
  if (SPIFFS.exists(JSON_CONFIG_Params)) {
    if (_Debug == true) Serial.println("[MIRA][FS] Lecture des fichiers de configuration des paramètres");
    File configFile = SPIFFS.open(JSON_CONFIG_Params, "r");
    if (configFile) {
      DeserializationError error = deserializeJson(SPIFFS_Config_Params, configFile);
      if (error)
        if (_Debug == true) Serial.println(F("[MIRA][FS] Lecture impossible"));
      configFile.close();
    }
  }
}



bool SPIFFS_EnregistrementReseau() {
  if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement de la configuration réseau...");
  JsonArray reseaux = SPIFFS_Config_Reseau["reseau"];
  JsonObject info = reseaux.createNestedObject();

  String cryptPass = SPIFFS_Crypt(SPIFFS_Pass);
  info["id"] = reseaux.size() - 1;
  info["ssid"] = SPIFFS_SSID;
  info["pass"] = cryptPass;
  info["ip"] = SPIFFS_IP;
  info["ip_mode"] = SPIFFS_IP_Mode;
  info["mask"] = SPIFFS_Mask;
  info["gateway"] = SPIFFS_Gateway;
  info["dns1"] = SPIFFS_DNS1;
  info["dns2"] = SPIFFS_DNS2;


  File configFile = SPIFFS.open(JSON_CONFIG_Reseau, "w");

  if (!configFile) {
    if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture...");
    return false;
  }

  if (serializeJson(SPIFFS_Config_Reseau, configFile) == 0) {
    if (_Debug == true) Serial.println(F("[MIRA][FS] Impossible d'écrire le fichier de configuration..."));
    return false;
  }
  configFile.close();
  if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Reseau, Serial);
  if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement terminé!");
  return true;
}

bool SPIFFS_RemoveReseau() {
  JsonArray Reseau = SPIFFS_Config_Reseau["reseau"];
  for (int i = 0; i < Reseau.size(); i++) {

    if (SPIFFS_Config_Reseau["reseau"][i]["id"].as<String>() == SPIFFS_ID) {
      Reseau.remove(i);
      File configFile = SPIFFS.open(JSON_CONFIG_Reseau, "w");

      if (!configFile) {
        if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration réseau pour écriture...");
        return false;
      }

      if (serializeJson(SPIFFS_Config_Reseau, configFile) == 0) {
        if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'écrire le fichier de configuration...");
        return false;
      }
      configFile.close();
      if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Reseau, Serial);
      if (_Debug == true) Serial.println("[MIRA][FS] Suppression terminée!");
      delay(200);
      return true;
    }
    delay(10);
  }
}


bool SPIFFS_UpdateReseau() {
  if (_Debug == true) Serial.println("[MIRA][FS] Modification de la configuration réseau...");
  JsonArray reseaux = SPIFFS_Config_Reseau["reseau"];
  for (int i = 0; i < reseaux.size(); i++) {
    if (SPIFFS_Config_Reseau["reseau"][i]["id"].as<String>() == SPIFFS_ID) {
      String CryptPass = SPIFFS_Crypt(SPIFFS_Pass);
      SPIFFS_Config_Reseau["reseau"][i]["ssid"] = SPIFFS_SSID;
      SPIFFS_Config_Reseau["reseau"][i]["pass"] = CryptPass;
      SPIFFS_Config_Reseau["reseau"][i]["ip"] = SPIFFS_IP;
       SPIFFS_Config_Reseau["reseau"][i]["ip_mode"] = SPIFFS_IP_Mode;
       SPIFFS_Config_Reseau["reseau"][i]["mask"] = SPIFFS_Mask;
       SPIFFS_Config_Reseau["reseau"][i]["gateway"] = SPIFFS_Gateway;
       SPIFFS_Config_Reseau["reseau"][i]["dns1"] = SPIFFS_DNS1;
       SPIFFS_Config_Reseau["reseau"][i]["dns2"] = SPIFFS_DNS2;

      File configFile = SPIFFS.open(JSON_CONFIG_Reseau, "w");
      if (!configFile) {
        if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration réseau pour écriture...");
        return false;
      }

      if (serializeJson(SPIFFS_Config_Reseau, configFile) == 0) {
        if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'écrire le fichier de configuration réseau...");
        return false;
      }
      configFile.close();
      if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Reseau, Serial);
      if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement terminé!");
      delay(200);
      return true;
    }
    delay(10);
  }
}

bool SPIFFS_EnregistrementAP() {
  if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement de la configuration réseau...");
  
  String CryptPass = SPIFFS_Crypt(Uncrypt_Pass);
  SPIFFS_Config_Reseau["ap"][0]["passcrypt"] = CryptPass;

  File configFile = SPIFFS.open(JSON_CONFIG_Reseau, "w");

  if (!configFile) {
    if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture...");
    return false;
  }

  if (serializeJson(SPIFFS_Config_Reseau, configFile) == 0) {
    if (_Debug == true) Serial.println(F("[MIRA][FS] Impossible d'écrire le fichier de configuration..."));
    return false;
  }
  configFile.close();
  if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Reseau, Serial);
  if (_Debug == true) Serial.println("[MIRA][FS] Enregistrement terminé!");
  return true;
}

void SPIFFS_CreationConfigReseau() {
  if (_Debug == true) Serial.println("[MIRA][FS] Création du fichier de configuration Réseau...");
  JsonArray reseau = SPIFFS_Config_Reseau.createNestedArray("reseau");
  JsonArray ap = SPIFFS_Config_Reseau.createNestedArray("ap");
  JsonObject Param = ap.createNestedObject();
  Param["passcrypt"] = "";
  Param["pass"] = "";
  Param["ip"] = "192.168.1.1";
  Param["actif"] = "0";

  File configFile = SPIFFS.open(JSON_CONFIG_Reseau, "w");

  if (!configFile) {
    if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture...");
  }

  if (serializeJson(SPIFFS_Config_Reseau, configFile) == 0) {
    if (_Debug == true) Serial.println(F("[MIRA][FS] Impossible d'écrire le fichier de configuration..."));
  }
  configFile.close();
  if (_Debug == true) serializeJsonPretty(SPIFFS_Config_Reseau, Serial);
  if (_Debug == true) Serial.println("[MIRA][FS] Création du fichier Réseau terminée!");
}

void SPIFFS_CreationConfigParams() {
  if (_Debug == true) Serial.println("[MIRA][FS] Création du fichier de configuration des paramètres...");
  JsonArray Params = SPIFFS_Config_Params.createNestedArray("params");



  File configFile = SPIFFS.open(JSON_CONFIG_Params, "w");

  if (!configFile) {
    if (_Debug == true) Serial.println("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture...");
  }

  if (serializeJson(SPIFFS_Config_Params, configFile) == 0) {
    if (_Debug == true) Serial.println(F("[MIRA][FS] Impossible d'écrire le fichier de configuration..."));
  }
  configFile.close();
  serializeJsonPretty(SPIFFS_Config_Params, Serial);
  if (_Debug == true) Serial.println("[MIRA][FS] Création du fichier terminée!");
}

void SPIFFS_ChargementConfigurationLvgl() {
  //Lecture du fichier Params
  if (SPIFFS.exists(JSON_CONFIG_lvgl)) {
    if (_Debug == true) Serial.println(F("[MIRA][FS] Lecture des fichiers de configuration lvgl"));
    File configFile = SPIFFS.open(JSON_CONFIG_lvgl, "r");
    if (configFile) {
      DeserializationError error = deserializeJson(mira_Json_LVGL, configFile);
      if (error)
        if (_Debug == true) Serial.println(F("[MIRA][FS] Lecture impossible"));
      configFile.close();
    }
  }
}

void SPIFFS_Creation_lvgl() {
  if (_Debug) Serial.println(F("[MIRA][FS] Création du fichier de configuration lvgl..."));
  JsonArray JSON_Array = mira_Json_LVGL.createNestedArray("lvgl");
  File configFile = SPIFFS.open(JSON_CONFIG_lvgl, "w");
  if (!configFile) {
    if (_Debug) Serial.println(F("[MIRA][FS] Impossible d'ouvrir le fichier de configuration pour écriture..."));
  }
  if (serializeJson(mira_Json_LVGL, configFile) == 0) {
    if (_Debug) Serial.println(F("[MIRA][FS] Impossible d'écrire le fichier de configuration..."));
  }
  configFile.close();
  serializeJsonPretty(mira_Json_LVGL, Serial);
  if (_Debug) Serial.println(F("[MIRA][FS] Création du fichier terminée!"));
}
