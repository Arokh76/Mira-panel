#pragma once

#if defined(MIRA_TARGET_WAVESHARE_43C)
#include <HTTPClient.h>

String ReponseAsync = "null";

bool miraAsyncHTTPGet(char *URL, bool GET = true) {
  if (!URL || !URL[0]) return false;

  HTTPClient http;
  http.setConnectTimeout(500);
  http.setTimeout(1000);
  if (!http.begin(URL)) {
    Serial.println(F("[MIRA][HTTP] Ouverture HTTP impossible"));
    return false;
  }

  int code = GET ? http.GET() : http.POST("");
  bool ok = code > 0;
  if (code == HTTP_CODE_OK) {
    ReponseAsync = http.getString();
  }
  http.end();

  if (!ok) Serial.println(F("[MIRA][HTTP] Requete HTTP impossible"));
  return ok;
}
#else
#include <AsyncHTTPRequest_Generic.h>

#define ASYNC_HTTP_REQUEST_GENERIC_VERSION_MIN_TARGET "AsyncHTTPRequest_Generic v1.10.2"
#define ASYNC_HTTP_REQUEST_GENERIC_VERSION_MIN 1010002

#define DEFAULT_RX_TIMEOUT 10
AsyncHTTPRequest request;
String ReponseAsync = "null";

void ReadRequest(void *optParm, AsyncHTTPRequest *request, int readyState) {
  (void)optParm;

  if (readyState == readyStateDone) {
    AHTTP_LOGDEBUG(F("\n**************************************"));
    AHTTP_LOGDEBUG1(F("Response Code = "), request->responseHTTPString());

    if (request->responseHTTPcode() == 200) {
      ReponseAsync = String(request->responseText());
      //Serial.println(ReponseAsync);
    }
  }
}


bool miraAsyncHTTPGet(char *URL, bool GET = true) {
  request.onReadyStateChange(ReadRequest);
  static bool requestOpenResult;
//Serial.println(URL);
  if (request.readyState() == readyStateUnsent || request.readyState() == readyStateDone) {
    //requestOpenResult = request.open("GET", "http://worldtimeapi.org/api/timezone/Europe/London.txt");
    if (GET) requestOpenResult = request.open("GET", URL);
    else requestOpenResult = request.open("POST", URL);
    if (requestOpenResult) {
      // Only send() if open() returns true, or crash
      request.send();
      return true;
    } else {
      Serial.println(F("[MIRA][HTTP] Envoi impossible, mauvais requête async"));
      return false;
    }
  } else {
    
    Serial.println(F("[MIRA][HTTP] Envoi de la requête async impossible"));
    return false;
  }
}

#endif
