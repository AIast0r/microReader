/*
 * Shared UTF-8 paginator for .md and .txt.
 * A .md "tex:" line remains an independent TeX-rendered page.
 * Both formats now use the same font-dependent line width/height and exact
 * page byte offsets, including backwards navigation. This avoids the old
 * "file.position() - 500" TXT rewind and mid-glyph page boundaries.
 */
#define MD_MAX_PAGES 96

struct MdPage {
  uint32_t start;
  uint32_t end;
  uint8_t kind;                   // 0 = UTF-8 text; 1 = "tex:" formula
};
static MdPage mdPages[MD_MAX_PAGES];
static uint16_t mdCount = 0;
static uint16_t mdCurrent = 0;
static bool mdTruncated = false;

static void mdPush(uint32_t start, uint32_t end, uint8_t kind) {
  if (end <= start) return;
  if (mdCount < MD_MAX_PAGES) {
    mdPages[mdCount].start = start;
    mdPages[mdCount].end = end;
    mdPages[mdCount].kind = kind;
    mdCount++;
  } else {
    mdTruncated = true;
  }
}

static bool mdIsTexLine(File& file) {
  const uint32_t pos = file.position();
  const bool isTex = file.read() == 't' && file.read() == 'e' &&
                     file.read() == 'x' && file.read() == ':';
  file.seek(pos);
  return isTex;
}

// Decode exactly one UTF-8 codepoint without cutting Cyrillic across pages.
static uint32_t mdReadCodepoint(File& file) {
  int value = file.read();
  if (value < 0) return 0;
  uint8_t lead = (uint8_t)value;
  if (lead < 0x80) return lead;
  uint32_t cp = 0;
  uint8_t extra = 0;
  if ((lead & 0xE0) == 0xC0) { cp = lead & 0x1F; extra = 1; }
  else if ((lead & 0xF0) == 0xE0) { cp = lead & 0x0F; extra = 2; }
  else if ((lead & 0xF8) == 0xF0) { cp = lead & 0x07; extra = 3; }
  else return '?';
  for (uint8_t i = 0; i < extra; i++) {
    int next = file.read();
    if (next < 0 || (((uint8_t)next & 0xC0) != 0x80)) return '?';
    cp = (cp << 6) | ((uint8_t)next & 0x3F);
  }
  return cp;
}

static void mdIndex(File& file, bool allowTeX) {
  file.seek(0);
  mdCount = mdCurrent = 0;
  mdTruncated = false;
  uint32_t pageStart = 0;
  uint8_t row = 0, col = 0;
  bool physicalLineStart = true;
  bool hasText = false;
  const uint8_t cols = readerTextCols();
  const uint8_t rows = readerTextRows();

  while (file.available()) {
    if (mdCount >= MD_MAX_PAGES) {
      mdTruncated = true;
      break;
    }

    const uint32_t pos = file.position();
    if (allowTeX && physicalLineStart && mdIsTexLine(file)) {
      if (hasText) mdPush(pageStart, pos, 0);
      while (file.available() && file.read() != '\n') {
        yield();
      }
      mdPush(pos, file.position(), 1);
      pageStart = file.position();
      row = col = 0;
      hasText = false;
      physicalLineStart = true;
      continue;
    }

    const uint32_t cp = mdReadCodepoint(file);
    if (cp == '\r') continue;
    if (cp == '\n') {
      ++row;
      col = 0;
      hasText = true;
      physicalLineStart = true;
      if (row >= rows) {
        mdPush(pageStart, file.position(), 0);
        pageStart = file.position();
        row = col = 0;
        hasText = false;
      }
      continue;
    }

    physicalLineStart = false;
    // Defer wrap until the following printable character. A newline at
    // exactly column 21/25/32 must not produce an additional empty row.
    if (col >= cols) {
      ++row;
      col = 0;
      if (row >= rows) {
        file.seek(pos);            // glyph belongs to the next page
        mdPush(pageStart, pos, 0);
        pageStart = pos;
        row = 0;
        hasText = false;
        continue;
      }
    }
    ++col;
    hasText = true;
    yield();
  }
  if (hasText && mdCount < MD_MAX_PAGES) {
    mdPush(pageStart, file.position(), 0);
  }
}

static void mdDrawText(File& file, uint16_t index) {
  const MdPage& page = mdPages[index];
  file.seek(page.start);
  oled.clear();
  oled.home();
  oled.autoPrintln(false);
  const uint8_t cols = readerTextCols();
  const uint8_t rows = readerTextRows();
  const uint8_t width = readerTextWidth();
  const uint8_t height = readerTextHeight();
  uint8_t row = 0, col = 0;

  while (file.position() < page.end && row < rows) {
    uint32_t cp = mdReadCodepoint(file);
    if (cp == '\r') continue;
    if (cp == '\n') {
      ++row;
      col = 0;
      if (readerFontMode == 0 && row < rows) oled.setCursor(0, row);
      continue;
    }
    if (col >= cols) {
      ++row;
      col = 0;
      if (readerFontMode == 0 && row < rows) oled.setCursor(0, row);
      if (row >= rows) break;
    }
    if (readerFontMode == 0) {
      readerWriteStandardGlyph(cp);
    } else {
      readerDrawCompactGlyph(cp, col * width, row * height);
    }
    ++col;
    yield();
  }
  oled.update();
  oled.autoPrintln(true);
}

static void mdDrawFormula(File& file, uint16_t index) {
  const MdPage& page = mdPages[index];
  file.seek(page.start);
  String formula = file.readStringUntil('\n');
  formula.remove(0, 4);              // discard "tex:" prefix
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

void enterToReadTextFile(bool allowTeX) {
  File file = LittleFS.open("/" + selectedFile, "r");
  if (!file) {
    fileReadError();
    checkFileSystem();
    drawMainMenu();
    return;
  }

  // On every file open, index its exact byte offsets using the saved profile.
  mdIndex(file, allowTeX);
  if (!mdCount) {
    file.close();
    texReleasePool();
    drawMainMenu();
    return;
  }
  mdDrawCurrent(file);
  while (true) {
    up.tick();
    ok.tick();
    down.tick();

    const bool exitReader = allowTeX ? ok.hold() : ok.click();
    if (exitReader) {
      if (allowTeX) waitOkRelease();
      uiTimer = millis();
      file.close();
      texReleasePool();
      drawMainMenu();
      return;
    }

    if (allowTeX && ok.click() && mdPages[mdCurrent].kind == 1 &&
        (texMaxX || texMaxY)) {
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
      if (allowTeX && mdPages[mdCurrent].kind == 1 &&
          texMode == TM_VERT) {
        texScrollY = constrain(texScrollY + direction * 8, 0, texMaxY);
        oled.clear();
        texDrawBody("");
        texDrawStatus(mdCurrent + 1, mdCount);
        oled.update();
      } else if (allowTeX && mdPages[mdCurrent].kind == 1 &&
                 texMode == TM_HORZ) {
        texScrollX = constrain(texScrollX + direction * 8, 0, texMaxX);
        oled.clear();
        texDrawBody("");
        texDrawStatus(mdCurrent + 1, mdCount);
        oled.update();
      } else {
        const int next = (int)mdCurrent + direction;
        if (next >= 0 && next < mdCount) {
          mdCurrent = next;
          mdDrawCurrent(file);
        }
      }
    }
    yield();
  }
}

void enterToReadMdFile(void) {
  enterToReadTextFile(true);
}
