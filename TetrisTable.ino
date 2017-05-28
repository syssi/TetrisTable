#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <NeoPixelBus.h>
#include "SimpleTimer.h"
#include "RenderFont.h"
#include <WebSocketsServer.h>
#include "index_html.h"

bool debug = true;
#define SerialDebug(text)   Serial.print(text);
#define SerialDebugln(text) Serial.println(text);

#define BAUD 9600
#define SERIAL_TIMEOUT 200
#define CONFIG_PORTAL_TIMEOUT 2 * 60
#define CONNECTION_TIMOUT 3000
#define REBOOT_TIMEOUT 5000

// GAME CONSTANTS
#define TEXT_INTERVAL 150
#define INITIAL_INTERVAL 1000
#define INTERVAL_DELTA 100
#define NUM_LINES_PER_LEVEL 10
#define MAX_STONES 7 // stone patterns: l     L     T     L^t   z     o     s
const uint8_t Stones[MAX_STONES] =     {0x0F, 0x2E, 0x4E, 0x8E, 0x6C, 0xCC, 0xC6};

// STATE
byte Mode = 1; // 1: Intro ticker, 2: tetris, 3: stats after game over
char MyIp[16];
char MyHostname[16];
int Lines = 0;
int Level = 1;

ESP8266WebServer server = ESP8266WebServer(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// DISPLAY
#define Width       13
#define Height      23
#define pixelCount (Width*Height)
#define pixelPin    3          // should be ignored because of NeoEsp8266Dma800KbpsMethod
NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod> strip(pixelCount, pixelPin); // this is on GPIO3 (RXD) due hardware limitations
NeoTopology <ColumnMajorAlternatingLayout> topo(Width, Height);
RgbColor black = RgbColor(0);

// data structure for current and next stone
struct sStone {
  int8_t   x, y;
  uint8_t  id;
  int8_t   dir;
  RgbColor col;
} Stone, NewStone;

// game timer
SimpleTimer TetrisTimer;
int TetrisTimerId;

// displays text
void ShowText() {
  TextPos++;
  if (TextPos > TextLen) {
    TextPos = 0;
  }
  strip.ClearTo(black);
  for (uint8_t x = 0 ; x < Height ; x++) {
    if (TextPos + x >= TextLen) {
      break;
    }
    for (int y = 0 ; y < 8 ; y++) {
      if ((Text[TextPos + x] & (1 << y)) != 0) {
        // to make better use of the display size, the text is displayed transposed, i.e. x and y are swapped here
        strip.SetPixelColor(topo.Map(y + TextOffsetY, x), TextCol);
      }
    }
  }
  strip.Show();
}

// GAME logic starts here
void MoveStoneDown();

// increase speed
void UpdateLevel() {
  int interval = INITIAL_INTERVAL - (INTERVAL_DELTA * Level);
  TetrisTimer.deleteTimer(TetrisTimerId);
  TetrisTimerId = TetrisTimer.setInterval(interval, MoveStoneDown);
}

// create new stone
void NextStone() {
  Stone.x = Width / 2;
  Stone.y = Height;
  Stone.id = random(0, MAX_STONES);
  Stone.dir = 0;
  Stone.col = RgbColor(random(5, 255), random(5, 255), random(5, 255));
}

// update
void CalcNewStone(uint8_t dir) {
  NewStone = Stone;
  switch (dir) {
    case 1: // Down
      NewStone.y--;
      break;
    case 2: // Right
      NewStone.x++;
      break;
    case 3: // Left
      NewStone.x--;
      break;
    case 4: // Rotate left
      NewStone.dir--;
      if (NewStone.dir < 0) {
        NewStone.dir = 3;
      }
      break;
    case 5: // Rotate right
      NewStone.dir++;
      if (NewStone.dir > 3) {
        NewStone.dir = 0;
      }
      break;
  }
}

uint8_t CheckSpace() {
  for (int8_t y = 0 ; y < 4 ; y++) {
    for (int8_t x = 0 ; x < 2 ; x++) {
      if ((Stones[NewStone.id] & (1 << (y + x * 4))) != 0) { // bit set in stone pattern?
        // compute all occupied x and y coordinates depending on stone's position and orientation
        int8_t x1 = NewStone.x, y1 = NewStone.y;
        switch (NewStone.dir) {
          case 0:
            x1 += x - 1;
            y1 += y - 2;
            break;
          case 1:
            x1 += y - 2;
            y1 -= x - 1;
            break;
          case 2:
            x1 -= x - 1;
            y1 -= y - 2;
            break;
          case 3:
            x1 -= y - 2;
            y1 += x - 1;
            break;
        }
        if (x1 < 0 || x1 >= Width) {
          return 1; // too far left / right
        }
        if (y1 < 0) {
          return 2; // bottom reached
        }
        if (strip.GetPixelColor(topo.Map(x1, y1)).CalculateBrightness() != 0) {
          return 3; // stone blocked
        }
      }
    }
  }
  return 0;
}

// removes full rows and downshifts everything above
void CheckRows() {
  for (int y = 0 ; y < Height;) {
    uint8_t cnt = 0;
    for (int x = 0 ; x < Width ; x++) {
      if (strip.GetPixelColor(topo.Map(x, y)).CalculateBrightness() != 0) {
        cnt++;
      }
    }
    if (cnt == Width) { // current row is full
      // shift everything down by one row
      for (int y1 = y ; y1 < Height - 1 ; y1++) {
        for (int x = 0 ; x < Width; x++) {
          strip.SetPixelColor(topo.Map(x, y1), strip.GetPixelColor(topo.Map(x, y1 + 1)));
        }
      }
      // set top most row to black after downshifting
      for (int x = 0 ; x < Width ; x++) {
        strip.SetPixelColor(topo.Map(x, Height), black);
      }
      strip.Show();
      Lines++;
      if ((Lines % NUM_LINES_PER_LEVEL) == 0) {
        Level++;
        UpdateLevel();
      }
      UpdateClient();
      continue;
    }
    y++;
  }
}

bool CheckGameOver() {
  return strip.GetPixelColor(topo.Map(Width / 2, Height - 1)).CalculateBrightness() != 0 ||
         strip.GetPixelColor(topo.Map(Width / 2 - 1, Height - 1)).CalculateBrightness() != 0;
}

// display stone (draw = true) or hide it
void DrawStone(bool draw) {
  // compute all occupied x and y coordinates depending on stone's position and orientation
  for (int8_t y = 0 ; y < 4 ; y++) {
    for (int8_t x = 0 ; x < 2 ; x++) {
      if ((Stones[Stone.id] & (1 << (y + x * 4))) != 0) { // bit set in stone pattern?
        int8_t x1 = Stone.x, y1 = Stone.y;
        switch (Stone.dir) {
          case 0:
            x1 += x - 1;
            y1 += y - 2;
            break;
          case 1:
            x1 += y - 2;
            y1 -= x - 1;
            break;
          case 2:
            x1 -= x - 1;
            y1 -= y - 2;
            break;
          case 3:
            x1 -= y - 2;
            y1 += x - 1;
            break;
        }
        if (y1 < Height) {
          strip.SetPixelColor(topo.Map(x1, y1), draw ? Stone.col : black);
        }
      }
    }
  }
  strip.Show();
}

// next game step
void MoveStoneDown() {
  CalcNewStone(1); // try to shift current stone down, store it in NewStone
  DrawStone(false); // hide stone Stone at its current position
  switch (CheckSpace()) { // check if position of NewStone is valid
    case 0: // OK! drops through to case 1!
    case 1: // too far left/right
      // this case is probably handled incorrectly.
      // however, it doesn't have any effect as the stones only move down, and therefore it is never triggered.
      Stone = NewStone; // this should go to case 0, followed by a break;
      break; // this should go away, so that case 1 drops through to case 2 and 3.
    case 2: // reached bottom. drops through to case 3!
    case 3: // stone blocked
      DrawStone(true); // display old stone again
      CheckRows(); // full row?
      if (CheckGameOver()) {
        GameOver();
        return;
      } else {
        NextStone();
      }
      break;
  }
  DrawStone(true); // display new stone
}

void NewGame() {
  TetrisTimer.deleteTimer(TetrisTimerId);
  TetrisTimerId = TetrisTimer.setInterval(INITIAL_INTERVAL, MoveStoneDown);
  strip.ClearTo(black);
  strip.Show();
  NextStone();
  NewStone = Stone;
  Lines = 0;
  Level = 1;
  UpdateLevel();
  UpdateClient();
}

// send # of lines and levels to web clients
void UpdateClient() {
  String s;
  s += "Lines: ";
  s += Lines;
  s += "<br />Level: ";
  s += Level;
  webSocket.broadcastTXT(s);
}

// forget WIFI (requires manual restart of the ESP)
void ResetWifi() {
  Mode = 1;
  WiFiManager wifiManager;
  wifiManager.resetSettings();

  setup();
}

// react to input from the web clients
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t len) {
  switch (type) {
    case WStype_DISCONNECTED:
      break;
    case WStype_CONNECTED: {
        IPAddress ip = webSocket.remoteIP(num);
        webSocket.sendTXT(num, "Connected");
        if (Mode != 2) {
          Mode = 2;
          NewGame();
        }
      }
      break;
    case WStype_TEXT: {
        // RenderText(payload, len);
        switch (*payload) {
          case 'd': // down
            CalcNewStone(1);
            break;
          case 'l': // left
            CalcNewStone(2);
            break;
          case 'r': // right
            CalcNewStone(3);
            break;
          case 'x': // rotate left
            CalcNewStone(4);
            break;
          case 'y': // rotate right
            CalcNewStone(5);
            break;
          case 'n': // new
            NewGame();
            break;
          case 'w': // reset
            ResetWifi();
            break;
        }
        DrawStone(false); // hide old stone
        switch (CheckSpace()) {
          case 0: // OK!
            // we can use the updated stone
            Stone = NewStone;
            break;
          case 1: // too far left/right
          case 2: // reached bottom
          case 3: // stone blocked
            break;
        }
        DrawStone(true); // draw updated stone
      }
      break;
  }
}

void GameOver() {
  char GameStats[MaxTextLen];
  TextPos = 0;
  snprintf(GameStats, MaxTextLen, "    GAME OVER   Lines %d   Level %d", Lines, Level);
  RenderText(GameStats);
  TextTimer.deleteTimer(TextTimerId);
  TextTimerId = TextTimer.setInterval(TEXT_INTERVAL, ShowText);
  Mode = 3; // 1?
}

String GetCredits() {
  return String("TEAM TETRIS  Basti, Fadime, Seppl");
}

void setup() {
  Serial.begin(BAUD);
  Serial.setTimeout(SERIAL_TIMEOUT);
  delay(100);

  // start wifi manager (make ESP an access point first to allow connecting to an existing wifi, or reconnect to a configured wifi)
  WiFiManager wifiManager;
  wifiManager.setDebugOutput(debug);
  wifiManager.setTimeout(CONFIG_PORTAL_TIMEOUT); // two minute timeout for the config portal
  if (!wifiManager.autoConnect()) { // attempt to connect to a wifi, reboot on failure
    delay(CONNECTION_TIMOUT);
    ESP.reset();
    delay(REBOOT_TIMEOUT);
  }
  // on connection success, display the ESP's IP address
  IPAddress MyIP = WiFi.localIP();
  snprintf(MyIp, 16, "%d.%d.%d.%d", MyIP[0], MyIP[1], MyIP[2], MyIP[3]);
  snprintf(MyHostname, 15, "ESP-%08x", ESP.getChipId());
  RenderText(WiFi.localIP().toString().c_str());

  // also send hostname over serial bus
  SerialDebug("ESP-Hostname: ");
  SerialDebugln(MyHostname);

  // set up text and game timers
  TextTimer.setInterval(TEXT_INTERVAL, ShowText);
  TetrisTimerId = TetrisTimer.setInterval(INITIAL_INTERVAL, MoveStoneDown);

  // start webserver & open websocket for communication
  server.on("/", []() {
    server.send(200, "text/html", index_html);
  });
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  strip.Begin();
  strip.ClearTo(black);
  strip.Show();
}

void loop() {
  server.handleClient();
  webSocket.loop();

  switch (Mode) {
    case 1:
      TextTimer.run();
      TextCol = RgbColor(random(1, 255), random(1, 255), random(1, 255));
      break;
    case 2:
      TetrisTimer.run();
      break;
    default:
      Mode = 1;
  }
}
