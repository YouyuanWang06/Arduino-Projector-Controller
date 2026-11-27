#include<SoftwareSerial.h>
#include "RevoCanNode.h"
#include <RevoLicenseClient.h>
SoftwareSerial mySerial (6,7);

static const uint8_t DEVICE_ID = 0x11; 
static const uint8_t LIC_MAX_ATTEMPTS = 8;

static uint8_t  licAttempts = 0;
static uint32_t licNextAttemptMs = 0;
static const uint8_t CAN_CS_PIN = 10;      // Nano typical MCP2515 CS

// Heartbeat
static const uint8_t  HB_TYPE = 0xF1;
static const uint32_t HEARTBEAT_MS = 3000;

// Debounce
static const uint16_t DEBOUNCE_MS = 100;
//CAN BUS code
static const uint8_t CMD_RESET         = 0x01;
static const uint8_t CMD_HEARTBEAT_REQ = 0x20;
static const uint8_t CMD_SET_GAMEMODE  = 0x12;
static const uint8_t CMD_CHANGE_LANG   = 0x40;
static const uint8_t CMD_POWER         = 0x50;
//static const uint8_t CMD_POWER_OFF   = 0x51;
static const uint8_t CMD_PREHEAT       = 0x52;
static const uint8_t CMD_PLAY          = 0x53;
static const uint8_t CMD_TEST_BUTTON   = 0x54;
//License
RevoLicenseClient license;
RevoCanNode node(CAN_CS_PIN, DEVICE_ID);

enum MasterState : uint8_t { PAUSE = 0, GAME = 1 };
static MasterState controllerState = GAME;

static void onCanCommand(const can_frame& msg);
static void buildHeartbeat(uint8_t* outData, uint8_t& outLen);

const int relayUp = 4, relayDown = 5;    // Up relay plugged into D4, Down relay plugged into D5
const int driverIn4 = 8, driverIn3 = 9; //if In4 is high, it means going down, if In3 is high, it means up. Brown ->3, Blue -> 4
const int upButton = A2, downButton = A3, testButton = A4; //buttons for testings
const int power = A5;

int scrDelay = 200; //to give relay 0.2 second of signal and change back to low
unsigned long driverDelay = 9000; //Driver takes 8 seconds, give an extra 1 second in case of signal delay
unsigned long videoLength = 126000; //length of video， can be changed

//for function isStableLow 
unsigned long DEBOUNCE_DELAY = 100; 

String language = "0";
int languageIndex = -1;

unsigned long powerTimer = 0;
unsigned long videoStartTime = 0;

//Flags
static bool preheatsig = false, playsig = false, powersig = false; // signals for poweron, play video. powersig for testing purose only

static bool screenReady = false; //only when screen is at 80% lowest point
static bool setScr = false, setPro = false; //signals for setting up screen and projector (moving down)
static bool resetScr = false, resetPro = false; //signals for resetting screen and projector (moving up) 
static bool playSequenceActive = false;
static bool isProjectorLow = false;

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(200); 
  mySerial.begin(9600);
  // put your setup code here, to run once:
  pinMode(upButton, INPUT_PULLUP);//Up
  pinMode(downButton, INPUT_PULLUP);//Down
  pinMode(testButton, INPUT_PULLUP);

  pinMode(relayUp, OUTPUT);
  pinMode(relayDown, OUTPUT);
  pinMode(power, OUTPUT);

  pinMode(driverIn4, OUTPUT);
  pinMode(driverIn3, OUTPUT);

  digitalWrite(relayUp, LOW);
  digitalWrite(relayDown, LOW);
  digitalWrite(power, LOW);

  Serial.print("Hi \nInitializing\n");
  digitalWrite(relayUp, HIGH);
  digitalWrite(driverIn3, HIGH);
  digitalWrite(driverIn4, LOW);
  delay(200);
  digitalWrite(relayUp, LOW);
  delay(driverDelay);
  digitalWrite(driverIn3, LOW);
  digitalWrite(driverIn4, LOW);

  node.begin(CAN_125KBPS);
  node.onCommand(onCanCommand);
  node.onBuildHeartbeat(buildHeartbeat);
  node.setHeartbeatIntervalMs(HEARTBEAT_MS);
  node.setJitterMs(50);
  node.setNumberOfDevices(20);

  delay(500 * DEVICE_ID);

  license.begin(DEVICE_ID);
  license.startAuth(node);

  node.requestHeartbeatNow();
  Serial.write("Done initializing\n");
}


void loop() {
  // put your main code here, to run repeatedly:
  node.update();
  license.update(node);

  if (!isLicensed()) {
    uint32_t now = millis();

    if (licAttempts < LIC_MAX_ATTEMPTS)
    {
        if ((int32_t)(now - licNextAttemptMs) >= 0)
        {
            // Attempt authentication
            license.startAuth(node, true);
            node.requestHeartbeatNow();

            // Calculate delay: 2500ms * 2^attempt
            uint32_t delayMs = 2500UL << licAttempts;

            licAttempts++;
            licNextAttemptMs = now + delayMs;
        }
    }
  }
  else
  {
      // Reset retry state once licensed
      licAttempts = 0;
      licNextAttemptMs = 0;
  }
  
  if (!isPowerOn()) playSequenceActive = false;

  Test_Button();
  checkSerial();
  preheat();
  screenSetup();
  playVideo();
  turnoff();
  strobe_light();
  //following code for testing purpose only:
  if(powersig){
    Serial.write("Turning on or off \n");
    digitalWrite(power, LOW);
    setScr = false;
    setPro = false;
    powersig = false;
  }
}

//to make sure the button and circuit is not affected by emi, minimum press time 0.2s
bool isStableLow(int pin, unsigned long &startTime, bool &triggered) {

  if (digitalRead(pin) == HIGH) {
    startTime = 0;
    triggered = false;
    return false;
  }

  if (triggered) {
    return false;
  }

  if (startTime == 0) {
    startTime = millis();
    return false;
  }

  if (millis() - startTime >= DEBOUNCE_DELAY) {
    triggered = true;
    return true;
  }

  return false;
}


void screenSetup (){
  static unsigned long scrDownTime = 0;
  static unsigned long proDownTime = 0;

  static bool isScreenDown = false; //check status if screen is moving down, do not mix up with screenReady
  static bool isDriverDown = false; //check status if driver is moving down
  static bool msgsent = false;

  if (millis()-powerTimer>=15000 && setScr && playsig){
    digitalWrite(relayDown, HIGH);
    delay(scrDelay);
    digitalWrite(relayDown, LOW);
    Serial.write("scr down\n");
    scrDownTime = millis();
    isScreenDown = true;
    setScr = false;
    playSequenceActive = true;
  }
  
  if (isScreenDown && millis()-scrDownTime>=24000 && setPro){
    digitalWrite(driverIn4, HIGH);
    digitalWrite(driverIn3, LOW);
    proDownTime = millis();    
    Serial.write("pro down\n");
    isDriverDown = true;    
    setPro = false;
  }
  if (isScreenDown && millis()-scrDownTime >= 31000){
    isScreenDown = false;
    scrDownTime = 0;
  }
  
  if (isDriverDown && millis()-proDownTime>=driverDelay){
    digitalWrite(driverIn4, LOW);
    digitalWrite(driverIn3, LOW);
    isDriverDown = false;
    isProjectorLow = true;
  }
  if (!msgsent && isScreenDown && (millis()-scrDownTime) >= 20000){//30 seconds for screen to be ready, 20 seconds to start playing black video with sounds
    screenReady = true;
    Serial.write("Screen Ready \n");
    msgsent = true;
  }
}
void playVideo (){
  if (screenReady && playsig){
    Serial.write("Start playing\n");
    String signal = "@" + updateLang() + "@";
    mySerial.write(signal.c_str());
    videoStartTime = millis();
    resetScr = true;
    resetPro = true;
    playsig = false;
  }
}

void turnoff(){
  static unsigned long scrUpTime = 0;
  static unsigned long proUpTime = 0;

  static bool isScreenUp = false; //check status for screen
  static bool isDriverUp = false; //check status for driver

  if(resetScr && (millis()-videoStartTime) >= (videoLength - 10000)){ //screen starts reset 10 seconds before video ends
    digitalWrite(relayUp, HIGH);
    Serial.write("scr up \n");
    isScreenUp = true;
    scrUpTime = millis();
    resetScr = false;
  }
  if(resetPro && (millis()-videoStartTime) >= videoLength){
    digitalWrite(driverIn3, HIGH);
    digitalWrite(driverIn4, LOW);
    Serial.write("pro up\n");
    proUpTime = millis();
    isDriverUp = true;
    screenReady = false;
    resetPro = false;
  }


  if (isScreenUp && millis()-scrUpTime>=scrDelay){
    digitalWrite(relayUp, LOW);
    isScreenUp = false;
  }
  if (isDriverUp && millis()-proUpTime>=driverDelay){
    Serial.write("turning off \n");
    digitalWrite(power, LOW);
    digitalWrite(driverIn3, LOW);
    digitalWrite(driverIn4, LOW);
    isDriverUp = false;
    isProjectorLow = false;
  }    
}
  
String updateLang(){
  if (languageIndex == -1){
    return language;
  }
  language = String(languageIndex); 
  languageIndex = -1;
  return language;
}



void preheat(){
  if (preheatsig){
    Serial.write("turning on \n");
    digitalWrite(power,HIGH);
    setScr = true;
    setPro = true;
    powerTimer = millis();
    preheatsig = false;
  }
}

void checkSerial(){
  if (Serial.available()>0){
    String order = Serial.readStringUntil(' ');
    order.trim();
    if (order == "PREHEAT"){
      preheatsig = true;
    }
    else if(order == "PLAY"){
      language = Serial.readStringUntil('\n');
      language.trim();
      playsig = true;
      languageIndex = -1;
    }
    else if(order == "POWER"){
      powersig = true;
    }
    else if(order == "POWERON"){
      digitalWrite(power,HIGH);
    }
    else if (order == "setup"){
      setScr = true;
      setPro = true;
    }
    else if (order == "print"){
      Serial.println(preheatsig);
      Serial.println(playsig);
      Serial.println(powersig);
      Serial.println(screenReady);
    }
  }  
}

void Test_Button(){
  static unsigned long timerA = 0;
  static unsigned long timerB = 0;
  static unsigned long timerC = 0;

  static bool triggeredA = false;
  static bool triggeredB = false;
  static bool triggeredC = false;

  static unsigned long upStartTime = 0;
  static unsigned long downStartTime = 0;
  static bool isDriverDown = false; //check status for screen
  static bool isDriverUp = false; //check status for driver

  if (isStableLow(upButton, timerA, triggeredA)){
    digitalWrite(driverIn3, HIGH);
    digitalWrite(driverIn4, LOW);
    Serial.print("Projector Up");
    isDriverUp = true;
    upStartTime = millis();
  }
  if (isDriverUp && upStartTime != 0 && millis()-upStartTime>=driverDelay){
    digitalWrite(driverIn3, LOW);
    digitalWrite(driverIn4, LOW);
    upStartTime = 0;
    Serial.print("Projector at top position");
    isDriverUp = false;
    isProjectorLow = false;
  }
  if (isStableLow(downButton, timerB, triggeredB)){
    digitalWrite(driverIn4, HIGH);
    digitalWrite(driverIn3, LOW);
    Serial.print("Projector down");
    isDriverDown = true;
    downStartTime = millis();
  }
  if (isDriverDown && downStartTime!=0 && millis()-downStartTime>=driverDelay){
    digitalWrite(driverIn4, LOW);
    digitalWrite(driverIn3, LOW);
    Serial.print("Projector at lowest position");
    downStartTime = 0;
    isDriverDown = false;
    isProjectorLow = true;
  }

  else if (isStableLow(testButton, timerC, triggeredC)){
    Serial.write("testButton pressed");
    preheatsig = true;
    playsig = true;
    languageIndex = 1;
  }
}

bool isPowerOn(){
  if (digitalRead(power)==HIGH){
    return true;
  }
  return false;
}

bool isVideoPlaying(){
  if (resetScr && resetPro){return true;}
  return false;
}

static void onCanCommand(const can_frame& msg) {
  // Always feed license client first
  license.onCanFrame(msg);
  // Serial.println(msg.can_id);
  // Serial.println(msg.data[0]);
  // Serial.println(msg.data[1]);

  if (msg.can_dlc < 2) return;
  if (msg.data[0]!=DEVICE_ID) return;
  uint8_t cmd = msg.data[1];
  uint8_t value = msg.data[2];

  // Not licensed: only allow heartbeat request + reset; keep outputs OFF
  if (!isLicensed()) {
    //allRelaysOff();
    if (cmd == CMD_HEARTBEAT_REQ) {
      node.requestHeartbeatNow();
      return;
    }
    if (cmd == CMD_RESET) {
      controllerState = GAME;
      license.clearAuth();
      license.startAuth(node, /*dropCurrent=*/true);
      node.requestHeartbeatNow();
      return;
    }
    return;
  }

  switch (cmd) {
    
    case CMD_RESET: {
      controllerState = GAME;
      digitalWrite(relayUp, LOW);
      digitalWrite(relayDown, LOW);
      digitalWrite(power, LOW);
      digitalWrite(driverIn3, HIGH);
      digitalWrite(driverIn4, LOW);
      license.clearAuth();
      license.startAuth(node, /*dropCurrent=*/true);
      node.requestHeartbeatNow();
      break;
    }

    case CMD_HEARTBEAT_REQ: {
      node.requestHeartbeatNow();
      break;
    }

    case CMD_SET_GAMEMODE: {
      if (msg.can_dlc < 3) break;
      controllerState = (msg.data[2] != 0) ? GAME : PAUSE;
      node.requestHeartbeatNow();
      break;
    }

    case CMD_CHANGE_LANG:{
      if (msg.can_dlc < 3) break;
      languageIndex = value;
    }

    // ---- WinForms light controls (auto PAUSE) ----
    case CMD_POWER: {
      if (msg.can_dlc < 3) break;
      if (value == 0x01){
        digitalWrite(power, HIGH);
      }
      if (value == 0x00){
        digitalWrite(power, LOW);
      }
      node.requestHeartbeatNow();
      break;
    }

    case CMD_PREHEAT: {
      preheatsig = true;

      node.requestHeartbeatNow();
      break;
    }

    case CMD_PLAY: {
      playsig = true;
      break;
    }

    //following for testing purpose, 
    case CMD_TEST_BUTTON: {
      if (!isPowerOn()){
        preheatsig = true;
      }
      playsig = true;
      
      node.requestHeartbeatNow();
      break;
    }

    default:
      break;
  }
}

static void buildHeartbeat(uint8_t* outData, uint8_t& outLen) {
  uint8_t statusFlags = 0;

  statusFlags |= (playSequenceActive ? 1 : 0) << 0;
  statusFlags |= (isLicensed() ? 1 : 0) << 1;
  statusFlags |= (screenReady ? 1 : 0) << 2;
  statusFlags |= (isProjectorLow ? 1 : 0) << 3;
  statusFlags |= (isPowerOn() ? 1 : 0) << 4;

  outData[0] = 0xF1;
  outData[1] = statusFlags;
  outData[2] = (byte)updateLang().toInt();

  outLen = 3;
}

static bool isLicensed() { return license.isAuthorized(); }
static bool isGameMode() { return controllerState == GAME; }

void strobe_light(){
  static bool Strobe_Msg_Sent = false;
  if ((!Strobe_Msg_Sent) && playSequenceActive && (millis()-videoStartTime>=110000)){
    uint8_t data[3];

    data[0] = 0x08; //sound and light controller
    data[1] = 0x42; //strobelight;
    data[2] = 0x01; //turn on;

    node.sendTelemetry(data, 3);
    Strobe_Msg_Sent = true;
  }
}
