/*
 * Lightweight mixed text + TeX reader for ESP8266 (independent implementation).
 *
 * .md content can contain normal UTF-8 text and lines beginning with "tex:".
 * A tex: line occupies its own page and is rendered with the imported TeX
 * engine. Text is paginated at 21 UTF-8 characters x 8 rows.
 * No Markdown tables/graphs or rich formatting: keep SRAM use predictable.
 */
#define MD_MAX_PAGES 160
#define MD_COLS 21
#define MD_ROWS 8

struct MdPage {
  uint32_t start;
  uint32_t end;
  uint8_t kind;                   // 0 = UTF-8 text, 1 = tex: formula
};
static MdPage mdPages[MD_MAX_PAGES];
static uint16_t mdCount = 0;
static uint16_t mdCurrent = 0;

static void mdPush(uint32_t start, uint32_t end, uint8_t kind) {
  if (mdCount < MD_MAX_PAGES && end > start) {
    mdPages[mdCount].start = start;
    mdPages[mdCount].end = end;
    mdPages[mdCount].kind = kind;
    mdCount++;
  }
}

static bool mdIsTexLine(File& file) {
  uint32_t p = file.position();
  bool isTex = file.read() == 't' &&
               file.read() == 'e' &&
               file.read() == 'x' &&
               file.read() == ':';
  file.seek(p);
  return isTex;
}

// UTF-8 lead byte length. Incomplete code points are treated as one glyph.
static uint8_t mdUtf8Bytes(uint8_t c) {
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

static void mdIndex(File& file) {
  file.seek(0);
  mdCount = mdCurrent = 0;
  uint32_t pageStart = 0;
  uint8_t row = 0, col = 0;
  bool lineStart = true, pageHasText = false;

  while (file.available() && mdCount < MD_MAX_PAGES) {
    uint32_t pos = file.position();
    if (lineStart && mdIsTexLine(file)) {
      if (pageHasText) mdPush(pageStart, pos, 0);
      // A formula extends to the end of its physical line.
      while (file.available()) {
        int c = file.read();
        if (c == '\n') break;
      }
      mdPush(pos, file.position(), 1);
      pageStart = file.position();
      row = col = 0;
      pageHasText = false;
      lineStart = true;
      continue;
    }

    int value = file.read();
    if (value < 0) break;
    uint8_t c = (uint8_t)value;
    if (c == '\r') continue;
    pageHasText = true;
    if (c == '\n') {
      row++;
      col = 0;
      lineStart = true;
    } else {
      uint8_t extra = mdUtf8Bytes(c) - 1;
      while (extra-- && file.available()) file.read();
      lineStart = false;
      col++;
      if (col >= MD_COLS) {
        col = 0;
        row++;
      }
    }
    if (row >= MD_ROWS) {
      mdPush(pageStart, file.position(), 0);
      pageStart = file.position();
      row = col = 0;
      pageHasText = false;
    }
    yield();
  }
  if (pageHasText && mdCount < MD_MAX_PAGES) mdPush(pageStart, file.position(), 0);
}

static void mdDrawText(File& file, uint16_t index) {
  const MdPage& page = mdPages[index];
  file.seek(page.start);
  oled.autoPrintln(false);
  oled.clear();
  oled.home();
  uint8_t row = 0, col = 0;
  while (file.position() < page.end && row < MD_ROWS) {
    int value = file.read();
    if (value < 0) break;
    uint8_t c = (uint8_t)value;
    if (c == '\r') continue;
    if (c == '\n') {
      row++;
      col = 0;
      if (row < MD_ROWS) oled.setCursor(0, row);
      continue;
    }
    // Emit the complete UTF-8 codepoint before advancing cursor, so a
    // multibyte Cyrillic glyph cannot be cut at a page boundary.
    oled.write(c);
    uint8_t extra = mdUtf8Bytes(c) - 1;
    while (extra-- && file.position() < page.end) oled.write(file.read());
    col++;
    if (col >= MD_COLS) {
      col = 0;
      row++;
      if (row < MD_ROWS) oled.setCursor(0, row);
    }
  }
  oled.update();
  oled.autoPrintln(true);
}

static void mdDrawFormula(File& file, uint16_t index) {
  const MdPage& page = mdPages[index];
  file.seek(page.start);
  String formula = file.readStringUntil('\n');
  formula.remove(0, 4);      // remove "tex:"
  formula.trim();
  memset(texLines, 0, sizeof(texLines));
  strncpy(texLines[0], formula.c_str(), TEX_LINE_LEN - 1);
  texLineCount = 1;
  texPage = 0;
  texNameCount = 1;
  texNames[0][0] = '\0';
  texMode = TM_PAGE;
  texLayoutFromLines("");
  oled.autoPrintln(false);
  oled.clear();
  texDrawBody("");
  texDrawStatus(mdCurrent + 1, mdCount);
  oled.update();
  oled.autoPrintln(true);
}

static void mdDrawCurrent(File& file) {
  if (mdCurrent >= mdCount) return;
  if (mdPages[mdCurrent].kind == 1) mdDrawFormula(file, mdCurrent);
  else mdDrawText(file, mdCurrent);
}

void enterToReadMdFile(void) {
  File file = LittleFS.open("/" + selectedFile, "r");
  if (!file) {
    fileReadError();
    checkFileSystem();
    drawMainMenu();
    return;
  }
  mdIndex(file);
  if (!mdCount) {
    file.close();
    drawMainMenu();
    return;
  }
  mdDrawCurrent(file);
  while (true) {
    up.tick();
    ok.tick();
    down.tick();
    if (ok.hold()) {
      waitOkRelease();
      uiTimer = millis();
      drawMainMenu();
      file.close();
      return;
    }
    if (ok.click() && mdPages[mdCurrent].kind == 1 && (texMaxX || texMaxY)) {
      uiTimer = millis();
      if (texMode == TM_PAGE) texMode = texMaxY ? TM_VERT : TM_HORZ;
      else if (texMode == TM_VERT) texMode = texMaxX ? TM_HORZ : TM_PAGE;
      else texMode = TM_PAGE;
      oled.clear();
      texDrawBody("");
      texDrawStatus(mdCurrent + 1, mdCount);
      oled.update();
    }
    int8_t direction = 0;
    if (up.click() || up.step()) direction = -1;
    if (down.click() || down.step()) direction = 1;
    if (direction) {
      uiTimer = millis();
      if (mdPages[mdCurrent].kind == 1 && texMode == TM_VERT) {
        texScrollY = constrain(texScrollY + direction * 8, 0, texMaxY);
        oled.clear(); texDrawBody(""); texDrawStatus(mdCurrent + 1, mdCount); oled.update();
      } else if (mdPages[mdCurrent].kind == 1 && texMode == TM_HORZ) {
        texScrollX = constrain(texScrollX + direction * 8, 0, texMaxX);
        oled.clear(); texDrawBody(""); texDrawStatus(mdCurrent + 1, mdCount); oled.update();
      } else {
        int next = (int)mdCurrent + direction;
        if (next >= 0 && next < mdCount) {
          mdCurrent = next;
          mdDrawCurrent(file);
        }
      }
    }
    yield();
  }
}
