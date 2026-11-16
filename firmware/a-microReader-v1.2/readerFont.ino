/*
 * Reader-only font profiles for ESP8266 / SSD1306 128x64.
 *
 * 0: legacy 6x8 = 21 columns x 8 rows
 * 1: compact 5x8 = 25 columns x 8 rows
 * 2: dense   5x7 = 25 columns x 9 rows
 * 3: tiny    4x6 = 32 columns x 10 rows (native Cyrillic/ASCII bitmap)
 *
 * Font selection lives in /font.dat, not in the existing /data.dat structure:
 * changing the original struct would reset existing Wi-Fi/game settings.
 */
#include "readerTinyFont.h"

uint8_t readerFontMode = 0;

uint8_t readerTextWidth() {
  switch (readerFontMode) {
    case 1:
    case 2: return 5;
    case 3: return 4;
    default: return 6;
  }
}

uint8_t readerTextHeight() {
  if (readerFontMode == 2) return 7;
  if (readerFontMode == 3) return 6;
  return 8;
}

uint8_t readerTextCols() { return 128 / readerTextWidth(); }
uint8_t readerTextRows() { return 64 / readerTextHeight(); }

void readerLoadFont() {
  readerFontMode = 0;  // backwards-compatible default
  File file = LittleFS.open("/font.dat", "r");
  if (!file) return;
  if (file.available() >= 2 && file.read() == 0xA5) {
    int mode = file.read();
    if (mode >= 0 && mode <= 3) readerFontMode = (uint8_t)mode;
  }
  file.close();
}

bool readerSetFont(uint8_t mode) {
  if (mode > 3) return false;
  if (readerFontMode == mode) return true;
  File file = LittleFS.open("/font.dat", "w");
  if (!file) {
    Serial.println(F("[FONT] unable to save /font.dat"));
    return false;
  }
  uint8_t bytes[2] = {0xA5, mode};
  const bool stored = file.write(bytes, sizeof(bytes)) == sizeof(bytes);
  file.close();
  if (stored) {
    readerFontMode = mode;
    Serial.printf_P(PSTR("[FONT] mode=%u cols=%u rows=%u\n"),
                    (unsigned)readerFontMode,
                    (unsigned)readerTextCols(),
                    (unsigned)readerTextRows());
  }
  return stored;
}

static uint32_t readerNormalizeCp(uint32_t cp) {
  switch (cp) {
    case 0x00A0: return ' ';
    case 0x2013:
    case 0x2014:
    case 0x2212: return '-';
    case 0x2018:
    case 0x2019: return '\'';
    case 0x201C:
    case 0x201D: return '"';
    case 0x00D7: return 'x';
    case 0x2192: return '>';
    case 0x2190: return '<';
    case 0x00B0: return 'o';
    default: return cp;
  }
}

static uint8_t readerGlyphIndex(uint32_t codepoint) {
  uint32_t cp = readerNormalizeCp(codepoint);
  if (cp >= 32 && cp <= 126) return (uint8_t)(cp - 32);
  if (cp >= 0x0410 && cp <= 0x042F) return (uint8_t)(95 + cp - 0x0410);
  if (cp >= 0x0430 && cp <= 0x044F) return (uint8_t)(127 + cp - 0x0430);
  if (cp == 0x0451) return 159;
  if (cp == 0x0401) return 100; // Ё -> Е (the original 5x8 map lacks uppercase Ё)
  return '?' - 32;
}

void readerDrawCompactGlyph(uint32_t codepoint, uint8_t x, uint8_t y) {
  uint8_t glyph = readerGlyphIndex(codepoint);
  if (readerFontMode == 3) {
    // Four ink pixels wide, six high; all glyphs (incl. Russian) are in flash.
    for (uint8_t col = 0; col < 4; col++) {
      uint8_t bits = pgm_read_byte(&readerTinyGlyphs[glyph][col]);
      for (uint8_t row = 0; row < 6; row++) {
        if (bits & (1U << row)) oled.dot(x + col, y + row, 1);
      }
    }
    return;
  }

  // Four ink columns + one pixel spacing. Preserve narrow letters and digits
  // without touching the default 6x8 menu font or the TeX renderer.
  const uint8_t a = pgm_read_byte(&_charMap[glyph][0]);
  const uint8_t b = pgm_read_byte(&_charMap[glyph][1]);
  const uint8_t c = pgm_read_byte(&_charMap[glyph][2]);
  const uint8_t d = pgm_read_byte(&_charMap[glyph][3]);
  const uint8_t e = pgm_read_byte(&_charMap[glyph][4]);
  const uint8_t cols[4] = {a, (uint8_t)(b | c), d, e};

  const uint8_t height = readerFontMode == 2 ? 7 : 8;
  for (uint8_t col = 0; col < 4; col++) {
    uint8_t mask = cols[col];
    for (uint8_t row = 0; row < height; row++) {
      // Preserve a descender from the original eighth row in 5x7 mode.
      bool enabled = (mask & (1U << row)) != 0;
      if (height == 7 && row == 6 && (mask & 0x80)) enabled = true;
      if (enabled) oled.dot(x + col, y + row, 1);
    }
  }
}

void readerWriteStandardGlyph(uint32_t codepoint) {
  uint32_t cp = readerNormalizeCp(codepoint);
  if (cp >= 32 && cp <= 126) {
    oled.write((uint8_t)cp);
  } else if ((cp >= 0x0410 && cp <= 0x044F) ||
             cp == 0x0451 || cp == 0x0401) {
    // GyverOLED_fix's existing Cyrillic decoder accepts UTF-8 byte sequences.
    oled.write((uint8_t)(0xC0 | (cp >> 6)));
    oled.write((uint8_t)(0x80 | (cp & 0x3F)));
  } else {
    oled.write((uint8_t)'?');
  }
}
