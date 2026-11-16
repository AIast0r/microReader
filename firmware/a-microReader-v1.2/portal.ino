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


// HTML-экранирование: имя загруженного файла нельзя вставлять в страницу как HTML.
static String readerHtmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (size_t i = 0; i < in.length(); i++) {
    switch (in[i]) {
      case '&': out += F("&amp;"); break;
      case '<': out += F("&lt;"); break;
      case '>': out += F("&gt;"); break;
      case '"': out += F("&quot;"); break;
      case '\'': out += F("&#39;"); break;
      default: out += in[i]; break;
    }
  }
  return out;
}

// Вместо штатного GP.FILE_MANAGER с таблицей шире окна выводим адаптивный
// список. Все операции с файлами выполняются прежним GyverPortal.
static void readerFileManager() {
  GP.SEND(F(
    "<style>"
    ".mr-list{display:flex;flex-direction:column;gap:6px;width:100%;max-width:100%;margin:10px 0;}"
    ".mr-file{box-sizing:border-box;display:grid;grid-template-columns:minmax(0,1fr) auto;"
    "align-items:center;gap:7px;width:100%;max-width:100%;border-radius:9px;"
    "background:#242831;padding:7px 9px;text-align:left;}"
    ".mr-info{min-width:0;overflow-wrap:anywhere;word-break:break-word;}"
    ".mr-name{color:#63d778;font-size:14px;text-decoration:none;line-height:1.35;}"
    ".mr-size{display:block;color:#aeb4be;font-size:12px;margin-top:2px;}"
    ".mr-actions{display:flex;flex-wrap:nowrap;gap:3px;align-items:center;}"
    ".mr-action{box-sizing:border-box;border:0;border-radius:6px;min-width:30px;"
    "height:30px;padding:2px 5px;background:#38404b;color:#fff;"
    "font-size:17px;text-align:center;text-decoration:none;cursor:pointer;}"
    ".mr-action.mr-danger{background:#6f313b;color:#fff;}"
    ".mr-protected{color:#aeb4be;font-size:12px;}"
    ".mr-usage{color:#adb6c3;text-align:center;font-size:13px;margin:10px 0 14px;}"
    ".mr-warning{font-size:13px;line-height:1.4;color:#d0d4de;margin:10px 0;overflow-wrap:anywhere;}"
    ".mr-dangerform{width:100%;max-width:100%;box-sizing:border-box;}"
    ".mr-dangerform input[type=submit]{max-width:100%;white-space:normal;font-size:15px;}"
    ".mr-checklabel{font-size:13px;line-height:1.35;overflow-wrap:anywhere;}"
    "@media(max-width:380px){.mr-file{gap:4px;padding:7px 5px;}"
    ".mr-action{min-width:27px;height:28px;padding:1px 4px;}}"
    "</style>"
    "<script>"
    "function mrDelFile(el){var p=el.getAttribute('data-path');"
    "if(confirm('Удалить '+p+'?'))GP_send('/GP_delete?'+encodeURIComponent(p),1);}"
    "function mrRenFile(el){var p=el.getAttribute('data-path');"
    "var n=prompt('Новое имя файла:',p.slice(p.lastIndexOf('/')+1));"
    "if(n===null)return;n=n.trim();"
    "if(!n||n.includes('/')||n.includes('\\\\')){alert('Укажите имя без / и обратного слеша');return;}"
    "GP_send('/GP_rename?'+encodeURIComponent(p)+'='+encodeURIComponent('/'+n),1);}"
    "</script>"
    "<div class='mr-list'>"
  ));

  Dir dir = LittleFS.openDir("/");
  uint16_t count = 0;
  while (dir.next()) {
    if (!dir.isFile()) continue;
    String name = dir.fileName();
    if (!name.startsWith("/")) name = "/" + name;
    const String safe = readerHtmlEscape(name);
    const bool protectedFile = (name == "/data.dat");

    String line;
    line.reserve(safe.length() * 3 + 320);
    line += F("<div class='mr-file'><div class='mr-info'><a class='mr-name' href='");
    line += safe;
    line += F("' download>");
    line += safe;
    line += F("</a><small class='mr-size'>");
    line += String(dir.fileSize() / 1024.0, 1);
    line += F(" KiB</small></div>");
    if (protectedFile) {
      line += F("<span class='mr-protected'>Настройки</span>");
    } else {
      line += F("<div class='mr-actions'><button type='button' class='mr-action'"
                " title='Переименовать' data-path='");
      line += safe;
      line += F("' onclick='mrRenFile(this)'>✎</button><a class='mr-action' title='Скачать' href='");
      line += safe;
      line += F("' download>↓</a><button type='button' class='mr-action mr-danger'"
                " title='Удалить' data-path='");
      line += safe;
      line += F("' onclick='mrDelFile(this)'>×</button></div>");
    }
    line += F("</div>");
    GP.SEND(line);
    count++;
    yield();
  }
  GP.SEND(F("</div>"));
  if (!count) GP.SEND(F("<p class='mr-usage'>Файлов пока нет</p>"));

  FSInfo fsi;
  if (LittleFS.info(fsi) && fsi.totalBytes) {
    String usage = F("<div class='mr-usage'>Память: ");
    usage += String(fsi.usedBytes / 1024.0, 1);
    usage += F(" / ");
    usage += String(fsi.totalBytes / 1024.0, 1);
    usage += F(" KiB (");
    usage += String((unsigned long)fsi.usedBytes * 100UL / fsi.totalBytes);
    usage += F("%)</div>");
    GP.SEND(usage);
  }
}

/* ======================================================================= */
/* ================= Билд веб-страницы + подсос значений ================= */
void build() {                     // Билд страницы
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
      M_BOX(GP_CENTER, GP.LABEL("Яркость"); GP.SLIDER("con", sets.dispContrast, 10, 100););););
  GP.FORM_END();                 // <- Конец формы (костыль)
  M_BLOCK_TAB(                   // Блок с OTA-апдейтом
    "Обновление прошивки",       // Имя + тип DIV
    GP.OTA_FIRMWARE();           // Кнопка с OTA начинкой
  );
  M_BLOCK_TAB(                   // Блок с файловым менеджером
    "Файловая система",          // Имя + тип DIV
    GP.FILE_UPLOAD("file_upl");  // Кнопка для загрузки файла
    readerFileManager();            // Компактный адаптивный список
    GP.HR();
    GP.SEND(F("<div class='mr-warning'>Удаление затрагивает только документы "
              "(.txt, .md, .tex, изображения). Настройки устройства сохраняются.</div>"));
    GP.SEND(F("<div class='mr-dangerform'>"));
    GP.FORM_BEGIN("/delete_user_files");
    M_BOX(GP_CENTER, GP.CHECK("confirm_delete", false);
          GP.SEND(F("<span class='mr-checklabel'>Подтверждаю удаление всех документов</span>")););
    GP.BREAK();
    GP.SUBMIT("Удалить все документы");
    GP.FORM_END();
    GP.SEND(F("</div>"));
    if (lastBulkDeleteCount) GP.LABEL(String("Удалено файлов: ") + lastBulkDeleteCount);
  );
  GP.BUILD_END();                  // Конец билда страницы
}

void action(GyverPortal& p) {      // Подсос значений со страницы
  if (p.form("/delete_user_files")) {
    bool confirmed = false;
    p.copyBool("confirm_delete", confirmed);
    if (confirmed) lastBulkDeleteCount = deleteAllUserFiles();
  }

  if (p.form("/cfg")) {            // Если есть сабмит формы - копируем все в переменные
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
