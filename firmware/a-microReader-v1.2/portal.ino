// Счётчик удалённых файлов для отображения в веб-интерфейсе.
static uint16_t lastBulkDeleteCount = 0;

// Не затрагиваем внутренний /data.dat с настройками и рекордами игр.
// На удаление допускаем только читаемые пользовательские файлы.
static bool isUserFileToDelete(const String& path) {
  return path.endsWith(".txt") || path.endsWith(".md") || path.endsWith(".tex") ||
         path.endsWith(".itxt") || path.endsWith(".h") ||
         path.endsWith(".jpg");
}

static uint16_t deleteAllUserFiles(void) {
  uint16_t removed = 0;
  // Открываем каталог заново после каждого удаления: на LittleFS итератор
  // может пропускать элементы при изменении списка во время обхода.
  for (uint16_t attempts = 0; attempts < 512; ++attempts) {
    Dir root = LittleFS.openDir("/");
    String target;
    while (root.next()) {
      String candidate = root.fileName();
      if (!candidate.startsWith("/")) candidate = "/" + candidate;
      if (isUserFileToDelete(candidate)) { target = candidate; break; }
    }
    if (!target.length() || !LittleFS.remove(target)) break;
    removed++;
    yield();
  }
  checkFileSystem();
  cursor = 0;
  return removed;
}


// Minimal, paginated FS listing. Never buffer the full directory as HTML.
// The full GyverPortal file table could exceed the ~10 KB free heap on ESP8266.
static String readerFileHtml(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (uint16_t i = 0; i < value.length(); i++) {
    switch (value[i]) {
      case '&': escaped += F("&amp;"); break;
      case '<': escaped += F("&lt;"); break;
      case '>': escaped += F("&gt;"); break;
      case '"': escaped += F("&quot;"); break;
      case '\'': escaped += F("&#39;"); break;
      default: escaped += value[i]; break;
    }
  }
  return escaped;
}

static String readerFileUrl(const String& path) {
  const char* const digits = "0123456789ABCDEF";
  String encoded;
  encoded.reserve(path.length() + 8);
  for (uint16_t i = 0; i < path.length(); i++) {
    uint8_t c = static_cast<uint8_t>(path[i]);
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '/' || c == '-' ||
        c == '_' || c == '.' || c == '~') encoded += char(c);
    else {
      encoded += '%';
      encoded += digits[c >> 4];
      encoded += digits[c & 0x0F];
    }
  }
  return encoded;
}

static void readerFileRow(const String& fullPath, size_t sizeBytes) {
  const String safeName = readerFileHtml(fullPath);
  const String safeUrl = readerFileHtml(readerFileUrl(fullPath));

  GP.SEND(F("<div class='rf-row'><div class='rf-name'><a href=\""));
  GP.SEND(safeUrl);
  GP.SEND(F("\" download>"));
  GP.SEND(safeName);
  GP.SEND(F("</a><small>"));
  GP.SEND(String(sizeBytes / 1024.0, 1));
  GP.SEND(F(" KiB</small></div><div class='rf-acts'>"));
  // Names are escaped as HTML attribute values, never used as JS source.
  GP.SEND(F("<button type='button' title='Переименовать' data-path=\""));
  GP.SEND(safeName);
  GP.SEND(F("\" onclick='rfRename(this)'>✎</button>"));
  GP.SEND(F("<a title='Скачать' href=\""));
  GP.SEND(safeUrl);
  GP.SEND(F("\" download>↓</a>"));
  GP.SEND(F("<button type='button' title='Удалить' data-path=\""));
  GP.SEND(safeName);
  GP.SEND(F("\" onclick='rfDelete(this)'>×</button></div></div>"));
}

// /files/<page>: 5 files max, with their own small web page.
// Main configuration page remains independent and known to work.
static void readerFilesPage() {
  const uint8_t pageSize = 5;
  String uri = ui.uri();
  long page = 0;
  if (uri.startsWith(F("/files/"))) page = uri.substring(7).toInt();
  if (page < 0) page = 0;
  if (page > 50) page = 50;
  uint16_t first = uint16_t(page) * pageSize;
  uint16_t seen = 0;
  uint8_t shown = 0;

  GP.BUILD_BEGIN(420);
  GP.THEME(GP_DARK);
  GP.PAGE_TITLE(F("Файлы microReader"));
  GP.SEND(F(
    "<style>"
    ".rf-list{width:100%;box-sizing:border-box;display:flex;flex-direction:column;gap:5px;}"
    ".rf-row{box-sizing:border-box;max-width:100%;width:100%;padding:7px 6px;"
    "display:flex;align-items:center;justify-content:space-between;gap:5px;"
    "background:#242830;border-radius:7px;text-align:left;}"
    ".rf-name{flex:1 1 0;min-width:0;overflow-wrap:anywhere;word-break:break-word;}"
    ".rf-name a{font-size:14px;color:#5fcb7e;text-decoration:none;}"
    ".rf-name small{font-size:11px;color:#aeb3bf;display:block;margin-top:2px;}"
    ".rf-acts{flex:none;display:flex;gap:3px;align-items:center;}"
    ".rf-acts button,.rf-acts a{cursor:pointer;border:0;text-decoration:none;"
    "border-radius:5px;min-width:26px;height:27px;padding:2px 4px;box-sizing:border-box;"
    "display:inline-flex;align-items:center;justify-content:center;background:#39404d;"
    "color:white;font-size:16px;}"
    ".rf-nav{margin:12px 0;display:flex;justify-content:center;gap:12px;}"
    ".rf-nav a{color:#a7dcae;}"
    "</style>"
    "<script>"
    "function rfDelete(b){var n=b.getAttribute('data-path');"
    "if(confirm('Удалить '+n+'?')) GP_send('/GP_delete?'+encodeURIComponent(n),1);}"
    "function rfRename(b){var n=b.getAttribute('data-path');"
    "var r=prompt('Новое имя файла',n.substring(n.lastIndexOf('/')+1));"
    "if(r===null)return;r=r.trim();"
    "if(!r||r.includes('/')||r.includes(String.fromCharCode(92)))"
    "{alert('Нужно имя без слешей');return;}"
    "GP_send('/GP_rename?'+encodeURIComponent(n)+'='+encodeURIComponent('/'+r),1);}"
    "</script>"
  ));
  GP.BLOCK_TAB_BEGIN(F("Файлы"));
  GP.BUTTON_LINK(F("/"), F("Назад к настройкам"));
  GP.FILE_UPLOAD(F("file_upl"), F("Добавить файлы"));
  GP.SEND(F("<div class='rf-list'>"));

  Dir root = LittleFS.openDir("/");
  while (root.next()) {
    if (!root.isFile()) continue;
    String fullPath = root.fileName();
    if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
    // Do not expose settings file or unrelated internal data in the browser.
    if (!isUserFileToDelete(fullPath)) continue;
    if (seen >= first && shown < pageSize) {
      readerFileRow(fullPath, root.fileSize());
      ++shown;
    }
    ++seen;
    yield();
  }
  GP.SEND(F("</div>"));
  if (!shown) GP.LABEL(F("На этой странице файлов нет"));
  // Use simple links to avoid extra per-file JS and avoid outputting all paths.
  GP.SEND(F("<div class='rf-nav'>"));
  if (page > 0) {
    GP.SEND(F("<a href='/files/"));
    GP.SEND(String(page - 1));
    GP.SEND(F("'>← Назад</a>"));
  }
  GP.SEND(F("Страница "));
  GP.SEND(String(page + 1));
  if (seen > first + shown) {
    GP.SEND(F("<a href='/files/"));
    GP.SEND(String(page + 1));
    GP.SEND(F("'>Далее →</a>"));
  }
  GP.SEND(F("</div>"));
  GP.LABEL(String(F("Всего документов: ")) + seen);
  GP.BLOCK_END();
  GP.BUILD_END();
}

// CSS/JS preview is stored in flash and rendered by the browser, not ESP RAM.
static const char readerFontPreviewHtml[] PROGMEM = R"PREV(
<style>
.rf-font-preview{width:100%;max-width:280px;margin:6px auto 10px;box-sizing:border-box;padding:7px;border-radius:8px;background:#10151a}
.rf-font-preview pre{font-family:monospace;white-space:pre;margin:0;width:100%;height:88px;overflow:hidden;color:#b2f4d2;font-size:11px;line-height:15px;text-align:left;box-sizing:border-box}
.rf-font-preview small{display:block;color:#a9b2bd;text-align:center;font-size:11px}
</style>
<div class="rf-font-preview"><small>Пример масштаба (условный)</small>
<pre id="rf-font-example">Термодинамическое
равновесие означает,
что параметры газа
не меняются.
pV = nRT
Температура: T, K.
Энергия: U = 3nRT/2
Давление: p = F/S</pre></div>
<script>
(function(){
 var sel=document.getElementById('readerFont'), demo=document.getElementById('rf-font-example');
 if(!sel||!demo)return;
 function preview(){
  var n=Number(sel.value)||0;
  demo.style.fontSize=[11,10,10,8][n]+'px';
  demo.style.lineHeight=[15,15,13,11][n]+'px';
 }
 sel.addEventListener('change',preview);preview();
})();
</script>
)PREV";

/* ======================================================================= */
/* ================= Билд веб-страницы + подсос значений ================= */
void build() {                     // Билд страницы
  if (ui.uri().startsWith(F("/files"))) {
    readerFilesPage();
    return;
  }
  Serial.printf_P(PSTR("[WEB] request BEGIN heap=%u largest=%u frag=%u%%\n"),
      ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), ESP.getHeapFragmentation());
  GP.BUILD_BEGIN(400);             // Ширина колонок
  GP.THEME(GP_DARK);               // Темная тема
  GP.PAGE_TITLE("Wi-Fi Reader");   // Обзываем титл
  GP.FORM_BEGIN("/cfg");           // Начало формы
  GP.GRID_RESPONSIVE(600);         // Отключение респонза при узком экране
  M_BLOCK(                         // Общий блок-колонка для WiFi
    GP.SUBMIT("Сохранить");        // Кнопка отправки формы
    M_BLOCK_TAB(                   // Конфиг для AP режима -> текстбоксы (логин + пароль)
      "Точка доступа",             // Имя + тип DIV
      GP.TEXT("apSsid", "Имя сети", sets.apSsid, "", 20);
      GP.BREAK();
      GP.TEXT("apPass", "Пароль", sets.apPass, "", 20); GP.BREAK(););
    M_BLOCK_TAB(                   // Конфиг для STA режима -> текстбоксы (логин + пароль)
      "Подключение к сети",        // Имя + тип DIV
      GP.TEXT("staSsid", "Имя сети", sets.staSsid, "", 20);
      GP.BREAK();
      GP.TEXT("staPass", "Пароль", sets.staPass, "", 20); GP.BREAK();
      M_BOX(GP_CENTER, GP.LABEL("Автоподключение"); GP.SWITCH("staEn", sets.staModeEn);););
    M_BLOCK_TAB(                   // Другие настройки
      "Другое",             // Имя + тип DIV
      M_BOX(GP_CENTER, GP.LABEL("Режим левши"); GP.SWITCH("leftEn", sets.leftmode);); GP.BREAK();
      M_BOX(GP_CENTER, GP.LABEL("Размер сегмента игр"); GP.NUMBER("tetrSeg", "", sets.tetrisSegment);); GP.BREAK(); // размер сегмента
      M_BOX(GP_CENTER, GP.LABEL("Яркость"); GP.SLIDER("con", sets.dispContrast, 10, 100);); GP.BREAK();
      GP.LABEL(F("Размер шрифта при чтении"));
      GP.SELECT(F("readerFont"), F("6x8 (21x8),5x8 (25x8),5x7 (25x9),4x6 (32x10)"), readerFontMode);
      GP.SEND_P(readerFontPreviewHtml);););
  GP.FORM_END();                 // <- Конец формы (костыль)
  M_BLOCK_TAB(                   // Блок с OTA-апдейтом
    "Обновление прошивки",       // Имя + тип DIV
    GP.OTA_FIRMWARE();           // Кнопка с OTA начинкой
  );
  M_BLOCK_TAB(                   // Блок с файловым менеджером
    "Файловая система",          // Имя + тип DIV
    GP.FILE_UPLOAD("file_upl");  // Кнопка для загрузки файла
    // DIAGNOSTIC: temporarily skip the complete filesystem HTML table.
    // This separates low-heap page generation from Wi-Fi / OTA crashes.
    // File upload, OTA, settings and confirmed bulk delete still work.
    GP.BUTTON_LINK(F("/files/0"), F("Управление файлами (по 5)"));
    GP.LABEL(F("Диагностика: список файлов временно отключён"));
    GP.BREAK();
    GP.LABEL(String(F("Файлов в каталоге чтения: ")) + fileCount);
    GP.BREAK();
    GP.LABEL(String(F("Причина прошлого сброса: ")) + ESP.getResetReason());
    GP.BREAK();
    GP.LABEL(String(F("Свободно RAM: ")) + ESP.getFreeHeap() + F(" байт"));
    GP.BREAK();
    GP.LABEL(String(F("Наибольший блок RAM: ")) + ESP.getMaxFreeBlockSize() + F(" байт"));
    GP.BREAK();
    GP.LABEL(String(F("Фрагментация RAM: ")) + ESP.getHeapFragmentation() + F("%"));
    GP.HR();
    GP.LABEL("Массовое удаление: только файлы книг и формул. Настройки сохраняются.");
    GP.FORM_BEGIN("/delete_user_files");
    M_BOX(GP_CENTER, GP.CHECK("confirm_delete", false); GP.LABEL("Подтверждаю удаление всех пользовательских файлов"););
    GP.SUBMIT("Удалить все пользовательские файлы");
    GP.FORM_END();
    if (lastBulkDeleteCount) GP.LABEL(String("Удалено файлов: ") + lastBulkDeleteCount);
  );
  GP.BUILD_END();                  // Конец билда страницы
  Serial.printf_P(PSTR("[WEB] request END heap=%u largest=%u frag=%u%%\n"),
      ESP.getFreeHeap(), ESP.getMaxFreeBlockSize(), ESP.getHeapFragmentation());
}

void action(GyverPortal& p) {      // Подсос значений со страницы
  if (p.click("readerFont")) {
    int requested = readerFontMode;
    if (p.copyInt("readerFont", requested) && requested >= 0 && requested <= 3) {
      readerSetFont((uint8_t)requested);
    }
  }

  if (p.form("/delete_user_files")) {
    bool confirmed = false;
    p.copyBool("confirm_delete", confirmed);
    if (confirmed) lastBulkDeleteCount = deleteAllUserFiles();
  }

  if (p.form("/cfg")) {
    int fontFromForm = readerFontMode;
    if (p.copyInt("readerFont", fontFromForm) &&
        fontFromForm >= 0 && fontFromForm <= 3) {
      readerSetFont((uint8_t)fontFromForm);
    }
            // Если есть сабмит формы - копируем все в переменные
    p.copyStr("apSsid", sets.apSsid);
    p.copyStr("apPass", sets.apPass);
    p.copyStr("staSsid", sets.staSsid);
    p.copyStr("staPass", sets.staPass);
    p.copyBool("staEn", sets.staModeEn);
    p.copyBool("leftEn", sets.leftmode);
    p.copyInt("con", sets.dispContrast);
    byte con = map(sets.dispContrast, 10, 100, 1, 255);
    oled.setContrast(con);            // Тут же задаем яркость оледа
    uint8_t tmp_tseg;
    p.copyInt("tetrSeg", tmp_tseg);
    if (tmp_tseg != sets.tetrisSegment) {      // если поменяли
      sets.tetrisSegment = tmp_tseg;           // записать
      WIDTH = 64 / tmp_tseg - 16 / tmp_tseg;   // новая ширина
      HEIGHT = 128 / tmp_tseg;                 // и высота
    }

    if (strlen(sets.apSsid) < 1)  strcpy(sets.apSsid, AP_DEFAULT_SSID);   // Проверка на пустое имя сети AP
    if (strlen(sets.apPass) < 8)  strcpy(sets.apPass, AP_DEFAULT_PASS);   // Проверка на пустой пасс AP

    if (sets.staModeEn) {                                                 // Если включен коннект
      if (strlen(sets.staSsid) < 1 || strlen(sets.staPass) < 8) {         // Проверка имени и пасса
        sets.staModeEn = false;                                           // Битое имя/пасс - выключаем коннект
      }
    }

    uint8_t contrast = map(sets.dispContrast, 10, 100, 1, 255);
    oled.setContrast(contrast);   // Тут же задаем яркость оледа
    //EEPROM.put(1, sets);        // Сохраняем все настройки в EEPROM
    //EEPROM.commit();            // Записываем
    data.update();                // Записать. Дальше оно само
  }
}
