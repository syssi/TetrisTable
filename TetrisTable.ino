#include <NeoPixelBus.h>
#include "SimpleTimer.h"
#include "fonts/6x8_vertikal_MSB_1.h"

#define SerialDebug(text) Serial.print(text);
#define SerialDebugln(text) Serial.println(text);

#define redButtonPin 12
#define blueButtonPin 14
#define downButtonPin 13

int redButtonState = 1;
int blueButtonState = 1;
int downButtonState = 0;

int redButtonPrevState = 1;
int blueButtonPrevState = 1;
int downButtonPrevState = 0;

int xAxisValue = 550; // neutral position
int xAxisState = 0;
int xAxisPrevState = 0;

#define Width       10
#define Height       17
#define pixelCount (Width*Height)
#define pixelPin    3 // should be ignored because of NeoEsp8266Dma800KbpsMethod
NeoPixelBus<NeoGrbFeature, NeoEsp8266Dma800KbpsMethod> strip(pixelCount, pixelPin); // this is on GPIO3 (RXD) due hardware limitations
NeoTopology <RowMajorAlternatingLayout> topo(Width, Height);
RgbColor black = RgbColor(0);

#define FontWidth 6
#define MaxTextLen  255
byte Text[MaxTextLen];
int TextPos = 0;
int TextLen = 0;
SimpleTimer TextTimer;
int TextTimerId;
RgbColor TextCol = RgbColor(128, 128, 128);
int TextOffsetY = 4; // pixel distance from bottom

SimpleTimer RunningLightTimer;
int RunningLightTimerId;
int RunningLightPos = 0;
RgbColor RunningLightCol = RgbColor(128, 128, 128);

byte Mode = 1; // 1: Intro ticker, 2: tetris, 3: stats after game over

// sets the bits in the text buffer accordinng to the character x
byte RenderLetter(byte * pos, const char x) {
  byte l = 0;
  for (int i = 0 ; i < FontWidth ; i++) {
    if (font[x][i] != 0) {
      pos[l] = font[x][i];
      l++;
    }
  }
  return l;
}

// prepares text buffer and other variables for rendering
void RenderText(const uint8_t * text, size_t len) {
  const uint8_t * p = text;
  TextPos = TextLen = 0;

  for (size_t i = 0 ; i < len && i < MaxTextLen - 8 ; i++) {
    TextLen += RenderLetter(&Text[TextLen], *p);
    // one row free space
    Text[TextLen] = 0;
    TextLen++;
    p++;
  }
}
void RenderText(const char * text) {
  RenderText((const uint8_t *) text, strlen(text));
}
void RenderText(String s) {
  RenderText((const uint8_t *)s.c_str(), s.length());
}

// displays the text
void ShowText() {
  TextPos++;
  if (TextPos > TextLen) {
    TextPos = 0;
  }
  strip.ClearTo(black);
  for (uint8_t x = 0 ; x < Width ; x++) {
    if (TextPos + x >= TextLen) {
      break;
    }
    for (int y = 0 ; y < 8 ; y++) {
      if ((Text[TextPos + x] & (1 << y)) != 0) {
        strip.SetPixelColor(topo.Map(Width - x - 1, y + TextOffsetY), TextCol);
      }
    }
  }
  strip.Show();
}

#define RatioFeeStone 10
#define MaxStones   7
const uint8_t Stones[MaxStones] = {0x0F, 0x2E, 0x4E, 0x8E, 0x6C, 0xCC, 0xC6}; // FEESTONE: , 0xAF};
struct sStone {
  int8_t    x, y;
  uint8_t   id;
  int8_t   dir;
  RgbColor  col;
} Stone, NewStone;

SimpleTimer TetrisTimer;
int TetrisTimerId;
int Lines = 0;
int Level = 1;

void MoveStoneDown();
void UpdateLevel() {
  int interval = 1000 - (50 * Level);
  TetrisTimer.deleteTimer(TetrisTimerId);
  TetrisTimerId = TetrisTimer.setInterval(interval, MoveStoneDown);
}

void DrawStone(bool draw) {
  for (int8_t y = 0 ; y < 4 ; y++) {
    for (int8_t x = 0 ; x < 2 ; x++) {
      if ((Stones[Stone.id] & (1 << (y + x * 4))) != 0) {
        int8_t x1 = Stone.x, y1 = Stone.y;
        switch (Stone.dir) {
          case 0: x1 += x - 1; y1 += y - 2; break;
          case 1: x1 += y - 2; y1 -= x - 1; break;
          case 2: x1 -= x - 1; y1 -= y - 2; break;
          case 3: x1 -= y - 2; y1 += x - 1; break;
        }
        if (y1 < Height) strip.SetPixelColor(topo.Map(x1, y1), draw ? Stone.col : black);
      }
    }
  }
  strip.Show();
}

void CalcNewStone(uint8_t dir) {
  NewStone = Stone;
  switch (dir) {
    case 1: NewStone.y--; break;    // Down (SM: was x--)
    case 2: NewStone.x++; break;    // Left (SM: was y++)
    case 3: NewStone.x--; break;    // Right (SM: was y--)
    case 4: NewStone.dir++;         // Rotate left
      if (NewStone.dir > 3) NewStone.dir = 0; break;
    case 5: NewStone.dir--;         // Rotate right
      if (NewStone.dir < 0) NewStone.dir = 3; break;
  }
}

void NextStone() {
  Stone.x = Width / 2;
  Stone.y = Height;
  Stone.id = random(0, MaxStones);
  Stone.dir = 0;
  Stone.col = RgbColor(random(5, 255), random(5, 255), random(5, 255));
}

uint8_t CheckSpace() {
  for (int8_t y = 0 ; y < 4 ; y++) {
    for (int8_t x = 0 ; x < 2 ; x++) {
      if ((Stones[NewStone.id] & (1 << (y + x * 4))) != 0) {
        int8_t x1 = NewStone.x, y1 = NewStone.y;
        switch (NewStone.dir) {
          case 0: x1 += x - 1; y1 += y - 2; break;
          case 1: x1 += y - 2; y1 -= x - 1; break;
          case 2: x1 -= x - 1; y1 -= y - 2; break;
          case 3: x1 -= y - 2; y1 += x - 1; break;
        }
        if (x1 < 0 || x1 >= Width) return 1; // too far left / right
        if (y1 < 0) return 2; // bottom reached
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
  for (int y = 0 ; y < Height ; ) {
    uint8_t cnt = 0;
    for (int x = 0 ; x < Width ; x++) {
      if (strip.GetPixelColor(topo.Map(x, y)).CalculateBrightness() != 0)
        cnt++;
    }
    if (cnt == Width) { // current row is full
      for (int y1 = y ; y1 < Height - 1 ; y1++) {
        for (int x = 0 ; x < Width ; x++) {
          // shift everything down by one row
          strip.SetPixelColor(topo.Map(x, y1), strip.GetPixelColor(topo.Map(x, y1 + 1)));
        }
      }
      // set top most row to black after downshifting
      for (int x = 0 ; x < Width ; x++) {
        strip.SetPixelColor(topo.Map(x, Height), black);
      }
      strip.Show();
      Lines++;
      if ((Lines % 10) == 0) {
        Level++;
        UpdateLevel();
      }
      continue;
    }
    y++;
  }
}

bool CheckGameOver() {
  return strip.GetPixelColor(topo.Map(Width / 2, Height - 1)).CalculateBrightness() != 0 ||
         strip.GetPixelColor(topo.Map(Width / 2 - 1, Height - 1)).CalculateBrightness() != 0;
}

void MoveStoneDown() {
  CalcNewStone(1);
  DrawStone(false);
  switch (CheckSpace()) {
    case 0:     // OK!
    case 1:     // Move too far left/right
      Stone = NewStone;
      break;
    case 2:     // reached bottom
    case 3:     // stone blocked
      DrawStone(true);
      CheckRows();
      if (CheckGameOver()) {
        GameOver();
        return;
      } else {
        NextStone();
      }
      break;
  }
  DrawStone(true);
}

void NewGame() {
  strip.ClearTo(black);
  strip.Show();
  NextStone();
  Lines = 0;
  Level = 1;
  UpdateLevel();
}

void GameOver() {
  char GameStats[MaxTextLen];
  TextPos = 0;
  snprintf(GameStats, MaxTextLen, "    GAME OVER   Lines %d   Level %d", Lines, Level);
  RenderText(GameStats);
  TextTimer.deleteTimer(TextTimerId);
  TextTimerId = TextTimer.setInterval(150, ShowText);
  Mode = 3;
}

String GetCredits() {
  return String("TEAM TETRIS  Basti, David, Dessi, Lisa, Michael A, Michael T, Ralf, Sarah, Seppl, Thilo, Verena K, Verena T");
}

void MoveRunningLight() {
  strip.SetPixelColor(RunningLightPos, black);
  RunningLightPos++;
  RunningLightPos = RunningLightPos % pixelCount;
  strip.SetPixelColor(RunningLightPos, RunningLightCol);
  strip.Show();
}

void StartRunningLight() {
  strip.ClearTo(black);
  RunningLightPos = 0;
  RunningLightCol = RgbColor(1, random(1, 255), 1);
  RunningLightTimer.setInterval(60, MoveRunningLight);
  RunningLightTimer.run();
}

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(200);
  delay(100);

  strip.Begin();
  strip.ClearTo(RgbColor(255));;
  strip.Show();

  pinMode(blueButtonPin, INPUT);
  pinMode(redButtonPin, INPUT);
  pinMode(downButtonPin, INPUT);

  RenderText((String("    FEETISCH       ")).c_str());
  TextTimerId = TextTimer.setInterval(150, ShowText);
  TetrisTimerId = TetrisTimer.setInterval(1000, MoveStoneDown);

  strip.ClearTo(black);
  strip.Show();
}

void loop() {
  redButtonState = digitalRead(redButtonPin);
  blueButtonState = digitalRead(blueButtonPin);
  downButtonState = digitalRead(downButtonPin);
  xAxisValue = analogRead(A0);

  if (xAxisValue < 640) {
    xAxisState = -1;
  } else if (xAxisValue > 880) {
    xAxisState = 1;
  } else {
    xAxisState = 0;
  }

  switch (Mode) {
    case 1: // intro
      if (blueButtonState != blueButtonPrevState) {
        if (blueButtonState == 0) {
          Mode = 2;
          NewGame();
        }
        blueButtonPrevState = blueButtonState;
      } else if (redButtonState != redButtonPrevState) {
        if (redButtonState == 0) {
          // TextPos = 0;
          // RenderText(GetCredits());
          // TextTimer.deleteTimer(TextTimerId);
          // TextTimerId = TextTimer.setInterval(150, ShowText);
          // Mode = 3;
        }
        redButtonPrevState = redButtonState;
      } else if (xAxisState != xAxisPrevState) {
        Mode = 4;
        xAxisPrevState = xAxisState;
        StartRunningLight();
        break;
      }
      TextTimer.run();
      TextCol = RgbColor(random(1, 255), random(1, 255), random(1, 255));
      break;
    case 2: // tetris game
      if (redButtonState != redButtonPrevState) {
        if (redButtonState == 0) {
          CalcNewStone(4);
          DrawStone(false);
          switch (CheckSpace()) {
            case 0:     // OK!
              Stone = NewStone;
              break;
            case 1:     // Move too far left/right
            case 2:     // reached bottom
            case 3:     // stone blocked
              break;
          }
          DrawStone(true);
        }
        redButtonPrevState = redButtonState;
      }

      if (blueButtonState != blueButtonPrevState) {
        if (blueButtonState == 0) {
          NewGame();
          DrawStone(false);
          switch (CheckSpace()) {
            case 0:     // OK!
              Stone = NewStone;
              break;
            case 1:     // Move too far left/right
            case 2:     // reached bottom
            case 3:     // stone blocked
              break;
          }
          DrawStone(true);
        }
        blueButtonPrevState = blueButtonState;
      }

      if (downButtonState != downButtonPrevState) {
        if (downButtonState == 1) {
          CalcNewStone(1);
          DrawStone(false);
          switch (CheckSpace()) {
            case 0:     // OK!
              Stone = NewStone;
              break;
            case 1:     // Move too far left/right
            case 2:     // reached bottom
            case 3:     // stone blocked
              break;
          }
          DrawStone(true);

        }
        downButtonPrevState = downButtonState;
      }

      if (xAxisState != xAxisPrevState) {
        // left
        if (xAxisState == -1) {
          CalcNewStone(2);
          DrawStone(false);
          switch (CheckSpace()) {
            case 0:     // OK!
              Stone = NewStone;
              break;
            case 1:     // Move too far left/right
            case 2:     // reached bottom
            case 3:     // stone blocked
              break;
          }
          DrawStone(true);
        }

        // right
        if (xAxisState == 1) {
          CalcNewStone(3);
          DrawStone(false);
          switch (CheckSpace()) {
            case 0:     // OK!
              Stone = NewStone;
              break;
            case 1:     // Move too far left/right
            case 2:     // reached bottom
            case 3:     // stone blocked
              break;
          }
          DrawStone(true);
        }

        xAxisPrevState = xAxisState;
      }

      TetrisTimer.run();
      break;
    case 3: // game stats
      if (blueButtonState != blueButtonPrevState) {
        if (blueButtonState == 0) {
          Mode = 2;
          NewGame();
        }
        blueButtonPrevState = blueButtonState;
      }
      TextTimer.run();
      TextCol = RgbColor(random(1, 255), random(1, 255), random(1, 255));
      break;
    case 4: // running light
      if (blueButtonState != blueButtonPrevState) {
        if (blueButtonState == 0) {
          Mode = 2;
          NewGame();
        }
        blueButtonPrevState = blueButtonState;
      }
      RunningLightTimer.run();
      break;
    default: Mode = 1;
  }
}

