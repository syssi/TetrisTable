#include "fonts/6x8_vertikal_MSB_1.h"

// FONT
#define FontWidth 6
int TextOffsetY = 2; // pixel distance from bottom
RgbColor TextCol = RgbColor(128, 128, 128);
int TextPos = 0;
int TextLen = 0;

#define MaxTextLen  255
byte Text[MaxTextLen];
SimpleTimer TextTimer;
int TextTimerId;


// sets the bits in the text buffer accordinng to the character x
byte RenderLetter(byte* pos, const char x) {
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
void RenderText(const uint8_t* text, size_t len) {
    const uint8_t* p = text;
    TextPos = TextLen = 0;
    
    for (size_t i = 0 ; i < len && i < MaxTextLen - 8 ; i++) {
        TextLen += RenderLetter(&Text[TextLen], *p);
        // one row free space
        Text[TextLen] = 0;
        TextLen++;
        p++;
    }
}
void RenderText(const char* text) {
    RenderText((const uint8_t*) text, strlen(text));
}
void RenderText(String s) {
    RenderText((const uint8_t*)s.c_str(), s.length());
}


