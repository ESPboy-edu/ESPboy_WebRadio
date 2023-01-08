/*
ESPboy_WebRadio www.ESPboy.com project by RomanS 12.08.2021
NOTE: compile with "v2 Higher Bandwidth" parameter
*/

#include <ESP8266WiFi.h>
#include "radioStations.h"
#include "lib/ESPboy_VS1053_Library/src/vs1053b_patches.h"
#include "lib/ESPboy_VS1053_Library/src/VS1053.h"
#include "lib/ESPboy_VS1053_Library/src/VS1053.cpp"
#include "lib/ESPboyInit.h"
#include "lib/ESPboyInit.cpp"
#include <ESP8266HTTPClient.h>
#include "lib/ESPboyTerminalGUI.h"
#include "lib/ESPboyTerminalGUI.cpp"
#include <EEPROM.h>

//VS1053 PINS on MCP23017
#define VS1053_CS     13
#define VS1053_DCS    15
#define VS1053_DREQ   12
#define LCD_CS        8

//Default parameters
#define DEFAULT_VOLUME  80
#define DEFAULT_STATION_NUMBER 0
#define BAD_CONNECTION_ON 2000 //size of data buffered
#define BAD_CONNECTION_OFF 3000 //size of data buffered
#define BAD_SIGNAL_DELAY 3000
#define UI_REDRAW_DELAY 500
#define OTA_TIMEOUT_CONNECTION 10000

//SAVINGS
#define OFFSET_MARKER 0
#define OFFSET_STATION_NUMBER 4
#define OFFSET_VOLUME 8
#define SAVE_MARKER 0xFCFF

uint16_t saveStationNumber = DEFAULT_STATION_NUMBER;
uint16_t saveVolume = DEFAULT_VOLUME;


ESPboyInit myESPboy;
VS1053 player(&myESPboy.mcp, LCD_CS, VS1053_CS, VS1053_DCS, VS1053_DREQ);
WiFiClient client;
ESPboyTerminalGUI terminalGUIobj(&myESPboy.tft, &myESPboy.mcp);


struct wificlient{
  String ssid;
  String pass;
};

struct wf {
    String ssid;
    uint8_t rssi;
    uint8_t encription;
};

struct lessRssi{
    inline bool operator() (const wf& str1, const wf& str2) {return (str1.rssi > str2.rssi);}
};

struct rS{
  String nameStation;
  String hostStation;
  uint16_t portStation;
  String pathStation;
};


std::vector<rS> rsList;
std::vector<wf> wfList; 
wificlient wificl;


// WiFi settings example
const char *ssid = "kolsky";
const char *password = "ice";


// Stream buffer
uint8_t mp3buff[255*20]  __attribute__ ((aligned(32)));


//load stations to the struct
void loadStationStruct(){
 char *pch;
  for (uint8_t i=0; i<sizeof(radioStList)/4; i++){
    pch = strtok((char *)radioStList[i], ";");
    if(atoi(pch) == 1) break;
    rsList.push_back(rS());
    rsList[i].nameStation = pch;
    pch = strtok(NULL,";");
    rsList[i].hostStation = pch;
    pch = strtok(NULL,";");
    rsList[i].portStation = atoi(pch);
    pch = strtok(NULL,";");
    rsList[i].pathStation = pch;
 }
}


void drawVolUI(){
    myESPboy.tft.fillRect (0, 120, 128-6*6, 8, TFT_BLACK);
    myESPboy.tft.setTextColor(TFT_GREEN, TFT_BLACK);
    String toPrint = "";
    for (uint8_t a=0; a<map(saveVolume, 0, 100, 0, 15); a++) 
      toPrint+="|";
    myESPboy.tft.drawString(toPrint, 0, 120);
}


void drawStaticUI(){
  myESPboy.tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
  myESPboy.tft.drawString("WiFi", 0, 0);
  myESPboy.tft.drawString("Stream", 92, 0);
  myESPboy.tft.drawString("Timer", 128-6*5, 110);
  myESPboy.tft.setTextColor(TFT_GREEN, TFT_BLACK);
  myESPboy.tft.drawString("Volume", 0, 110);
}


void drawDynamicUI(uint32_t dataAvailable, int32_t saveVolume, bool badConnection, uint32_t timerDelayBadConnection){
 static String toPrint;
 
    myESPboy.tft.fillRect (0, 10, 128, 8, TFT_BLACK);
    myESPboy.tft.fillRect (128-6*6, 120, 128, 8, TFT_BLACK);
    
    myESPboy.tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    toPrint="";
    for (uint8_t a=0; a<map(100+WiFi.RSSI(), 0, 100, 0, 10); a++) 
      toPrint+="|";
    myESPboy.tft.drawString(toPrint, 0, 10);
    
    toPrint = "";
    for (uint8_t a=0; a<map(dataAvailable, 0, 6000, 0, 10); a++) 
      toPrint+="|";
    myESPboy.tft.drawString(toPrint, 128-6*(toPrint.length()-2), 10);
    
    toPrint=(String)(millis()/1000);
    myESPboy.tft.drawString(toPrint, 128-6*toPrint.length(), 120);

    if (badConnection == true && (millis()-timerDelayBadConnection) > BAD_SIGNAL_DELAY){
      myESPboy.tft.setTextColor(TFT_RED, TFT_BLACK);
      myESPboy.tft.drawString("BAD CONNECTION!", (128-6*15)/2, 100);
    }
    else
      myESPboy.tft.drawString("               ", (128-6*15)/2, 100);
}


void loadSaves(){
 uint32_t readedMarker;
  EEPROM.get(OFFSET_MARKER, readedMarker);
  if(readedMarker == SAVE_MARKER){
    EEPROM.get(OFFSET_STATION_NUMBER, saveStationNumber);
    EEPROM.get(OFFSET_VOLUME, saveVolume);
  }
  else saveSaves();
}


void saveSaves(){
  EEPROM.put(OFFSET_MARKER, SAVE_MARKER);
  EEPROM.put(OFFSET_VOLUME, saveVolume); 
  EEPROM.put(OFFSET_STATION_NUMBER, saveStationNumber);  
  EEPROM.commit();
}



uint16_t scanWiFi() {
  terminalGUIobj.printConsole(F("Scaning WiFi..."), TFT_MAGENTA, 1, 0);
  int16_t WifiQuantity = WiFi.scanNetworks();
  if (WifiQuantity != -1 && WifiQuantity != -2 && WifiQuantity != 0) {
    for (uint8_t i = 0; i < WifiQuantity; i++) wfList.push_back(wf());
    if (!WifiQuantity) {
      terminalGUIobj.printConsole(F("WiFi not found"), TFT_RED, 1, 0);
      delay(3000);
      ESP.reset();
    } else
      for (uint8_t i = 0; i < wfList.size(); i++) {
        wfList[i].ssid = WiFi.SSID(i);
        wfList[i].rssi = WiFi.RSSI(i);
        wfList[i].encription = WiFi.encryptionType(i);
        delay(0);
      }
    sort(wfList.begin(), wfList.end(), lessRssi());
    return (WifiQuantity);
  } else
    return (0);
}

  

bool wifiConnect() {
 uint16_t wifiNo = 0;
 uint32_t timeOutTimer;
 static uint8_t connectionErrorFlag = 0;

  //wificl = new wificlient();
  
  if (!connectionErrorFlag || !(terminalGUIobj.getKeys()&PAD_ESC)) {
    wificl.ssid = WiFi.SSID();
    wificl.pass = WiFi.psk();
    terminalGUIobj.printConsole(F("Last network:"), TFT_MAGENTA, 0, 0);
    terminalGUIobj.printConsole(wificl.ssid, TFT_GREEN, 0, 0);
  } 
  else 
  {
      wificl.ssid = "";
      wificl.pass = "";
    
    if (scanWiFi())
      for (uint8_t i = wfList.size(); i > 0; i--) {
        String toPrint =
            (String)(i) + " " + wfList[i - 1].ssid + " " + wfList[i - 1].rssi +
            "" + ((wfList[i - 1].encription == ENC_TYPE_NONE) ? "" : "*");
        terminalGUIobj.printConsole(toPrint, TFT_YELLOW, 0, 0);
    }

    while (!wifiNo) {
      terminalGUIobj.printConsole(F("Choose WiFi No:"), TFT_MAGENTA, 0, 0);
      wifiNo = terminalGUIobj.getUserInput().toInt();
      if (wifiNo < 1 || wifiNo > wfList.size()) wifiNo = 0;
    }

    wificl.ssid = wfList[wifiNo - 1].ssid;
    terminalGUIobj.printConsole(wificl.ssid, TFT_GREEN, 1, 0);

    while (!wificl.pass.length()) {
      terminalGUIobj.printConsole(F("Password:"), TFT_MAGENTA, 0, 0);
      wificl.pass = terminalGUIobj.getUserInput();
    }
    terminalGUIobj.printConsole(/*pass*/F("******"), TFT_GREEN, 0, 0);
  }

  wfList.clear();
  wfList.shrink_to_fit();

  WiFi.mode(WIFI_STA);
  WiFi.begin(wificl.ssid, wificl.pass);

  terminalGUIobj.printConsole(F("Connection..."), TFT_MAGENTA, 0, 0);
  timeOutTimer = millis();
  String dots = "";
  while (WiFi.status() != WL_CONNECTED &&
         (millis() - timeOutTimer < OTA_TIMEOUT_CONNECTION)) {
    delay(700);
    terminalGUIobj.printConsole(dots, TFT_MAGENTA, 0, 1);
    dots += ".";
    if (dots.length()>20) dots=".";
  }

  if (WiFi.status() != WL_CONNECTED) {
    connectionErrorFlag = 1;
    terminalGUIobj.printConsole(getWiFiStatusName(), TFT_RED, 0, 1);
    delay(1000);
    terminalGUIobj.printConsole("", TFT_BLACK, 0, 0);
    return (false);
  } else {
    terminalGUIobj.printConsole(getWiFiStatusName(), TFT_MAGENTA, 0, 1);
    return (true);
  }

}



String getWiFiStatusName() {
  String stat;
  switch (WiFi.status()) {
    case WL_IDLE_STATUS:
      stat = (F("Idle"));
      break;
    case WL_NO_SSID_AVAIL:
      stat = (F("No SSID available"));
      break;
    case WL_SCAN_COMPLETED:
      stat = (F("Scan completed"));
      break;
    case WL_CONNECTED:
      stat = (F("WiFi connected"));
      break;
    case WL_CONNECT_FAILED:
      stat = (F("Wrong passphrase"));
      break;
    case WL_CONNECTION_LOST:
      stat = (F("Connection lost"));
      break;
    case WL_DISCONNECTED:
      stat = (F("Wrong password"));
      break;
    default:
      stat = (F("Unknown"));
      break;
  };
  return stat;
}


void setup() {
  Serial.begin(115200);
  EEPROM.begin(100);
  
  myESPboy.begin("Web Radio");
  player.begin();

  terminalGUIobj.printConsole("ESPboy WebRadio v1.0", TFT_GREEN, 0, 0);
  
  if(player.isChipConnected()) {terminalGUIobj.printConsole("Init OK",TFT_GREEN,0,20);}
  else {terminalGUIobj.printConsole("Init FAIL", TFT_RED, 0,20); while(1) delay(1000);}

  terminalGUIobj.printConsole("", TFT_BLACK, 0, 0);
    
  player.loadDefaultVs1053Patches();
  player.switchToMp3Mode();
  player.setVolume(0);

  while (!wifiConnect());

  loadStationStruct();
  loadSaves();
  
  player.setVolume(saveVolume);
  
  myESPboy.tft.fillScreen(TFT_BLACK);
  drawStaticUI();
}


void loop() {
  static uint32_t timerDelayUI;
  static uint32_t timerDelayBadConnection;
  static uint32_t dataAvailable;
  static bool badConnection = false;
  static uint16_t getedVolume;
  
//Reconnect if connection lost or station change
  if (!client.connected()){
    if (badConnection == false) {saveVolume = player.getVolume(); badConnection = true;}
    player.setVolume(0);
    myESPboy.tft.fillScreen(TFT_BLACK);
    drawStaticUI();
    drawVolUI();  
    myESPboy.tft.setTextColor(TFT_RED, TFT_BLACK);
    myESPboy.tft.drawString("    Reconnect...    ", (128-20*6)/2, 100);
    myESPboy.tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    String toPrint = (String)(saveStationNumber+1) + ". " + rsList[saveStationNumber].nameStation;
    myESPboy.tft.drawString(toPrint,(128-toPrint.length()*6)/2,50);
    if (client.connect(rsList[saveStationNumber].hostStation.c_str(), rsList[saveStationNumber].portStation)) 
      client.print(String("GET ") + rsList[saveStationNumber].pathStation + " HTTP/1.1\r\n" + "Host: " + rsList[saveStationNumber].hostStation + "\r\n" + "Connection: close\r\n\r\n");
    timerDelayBadConnection = millis();}

//Download stream to buffer and upload buffer to VS1053
  dataAvailable = client.available();
  if (dataAvailable)
    player.playChunk(mp3buff, client.readBytes(mp3buff, sizeof(mp3buff)));

//Read button commands and save changings
  if(myESPboy.getKeys()){
    if(myESPboy.getKeys()&PAD_RIGHT &&  saveVolume<95)  {
      myESPboy.playTone(50,50); 
      saveVolume+=5; 
      drawVolUI();}
    if(myESPboy.getKeys()&PAD_LEFT && saveVolume>5) {
      myESPboy.playTone(50,50); 
      saveVolume-=5; 
      drawVolUI();}

    if(myESPboy.getKeys()&PAD_UP && saveStationNumber < rsList.size()-1) {
      saveStationNumber++; 
      client.stop(); 
      while(myESPboy.getKeys())delay(10);
    }
    
    if(myESPboy.getKeys()&PAD_DOWN && saveStationNumber>0) {
      saveStationNumber--;  
      client.stop(); 
      while(myESPboy.getKeys())delay(10);
    }
    saveSaves();
  }

//Bad connection detection, sound fading and restore to previously set
  if (dataAvailable < BAD_CONNECTION_ON && badConnection == false) 
    badConnection = true;

  if (dataAvailable > BAD_CONNECTION_OFF && badConnection == true && getedVolume == saveVolume)
    badConnection = false;

  if (dataAvailable < BAD_CONNECTION_ON)
    player.setVolume(0);

getedVolume = player.getVolume();

//Smooth volume control
  if(abs(saveVolume-getedVolume)>4){
    if (saveVolume > getedVolume) player.setVolume(getedVolume + abs(saveVolume-getedVolume) /2 + 1);
    if (saveVolume < getedVolume) player.setVolume(getedVolume - abs(saveVolume-getedVolume) /2 - 1);}
  else{
    if (saveVolume > getedVolume) player.setVolume(getedVolume+1);
    if (saveVolume < getedVolume) player.setVolume(getedVolume-1);}
       
//Redraw UI every UI_REDRAW_DELAY sec
  if(millis()-timerDelayUI > UI_REDRAW_DELAY){
    timerDelayUI = millis();
    drawDynamicUI(dataAvailable, saveVolume, badConnection, timerDelayBadConnection);
  }
}
