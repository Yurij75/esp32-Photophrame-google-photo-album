// 🖼️ ESP32-S3 Google Photos Frame с веб-настройками (Arduino GFX версия)
#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <vector>
#include <Arduino_GFX_Library.h>
#include <TJpg_Decoder.h>

// ==================== НАСТРОЙКИ ДИСПЛЕЯ ====================
// Пины из вашего конфига
#define TFT_DC   9  
#define TFT_CS   10
#define TFT_RST  -1
#define TFT_BL   14
#define TFT_MOSI 11
#define TFT_SCLK 12
// #define TFT_ROTATION 2
#define TFT_BRIGHTNESS 255

// Настройки WiFi из вашего кода
const char* ssid = "Tibor";
const char* password = "fox25011970";

// // Инициализация дисплея с Arduino GFX (ST7796)
// Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI);
// Arduino_GFX *gfx = new Arduino_ST7796(bus, TFT_RST, 1 /* rotation */, false /* IPS */);

// Инициализация дисплея с Arduino GFX (ST7789 с инверсией)
Arduino_DataBus *bus = new Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI); // -1 для MISO (не используется)
// Для ST7789 с инверсией используем конструктор с параметром для инверсии
Arduino_GFX *gfx = new Arduino_ST7789(bus, TFT_RST, 3 /* rotation */, true /* IPS */);

WebServer server(80);
Preferences preferences;

// ==================== НАСТРОЙКИ ФОТОРАМКИ ====================

// Настройки из памяти
String albumUrl = "";               // URL альбома теперь ТОЛЬКО из вебинтерфейса
unsigned long slideshowInterval = 5000;
bool singlePhotoMode = false;
int selectedPhotoIndex = 0;
int transitionEffect = 0;
bool randomMode = false;           // Режим случайного показа

// Система порционной загрузки
const int CHUNK_SIZE = 50;
std::vector<String> photoUrls;
int currentChunkStart = 0;
int totalPhotosInAlbum = 0;
int currentPhotoIndex = 2;         // СТАРТУЕМ С 2 (пропуск фото 0 и 1)

unsigned long lastChange = 0;
String cachedHTML = "";

// Для режима случайного показа
std::vector<int> availablePhotoIndices;
int currentRandomIndex = 0;

// Буфер изображений
uint16_t* currentPhotoBuffer = nullptr;
uint16_t* nextPhotoBuffer = nullptr;
bool nextPhotoLoaded = false;
uint8_t* jpegBuffer = nullptr;

// Защита от зависания
int failedLoadAttempts = 0;
const int MAX_FAILED_ATTEMPTS = 3;

// ==================== ВЕБ-ИНТЕРФЕЙС ====================

const char* htmlPage = R"(
<!DOCTYPE html>
<html lang='ru'>
<head>
<meta charset='UTF-8'>
<meta name='viewport' content='width=device-width, initial-scale=1.0'>
<title>ESP32-S3 Google Photos Frame</title>
<style>
body{font-family:Arial,sans-serif;max-width:600px;margin:50px auto;padding:20px;background:#f0f0f0}
h1{color:#333;text-align:center}
.card{background:#fff;padding:20px;margin:15px 0;border-radius:8px;box-shadow:0 2px 4px rgba(0,0,0,0.1)}
label{display:block;margin:10px 0 5px;font-weight:bold;color:#555}
input,select{width:100%;padding:10px;border:1px solid #ddd;border-radius:4px;box-sizing:border-box;font-size:14px}
button{background:#4CAF50;color:#fff;padding:12px 20px;border:none;border-radius:4px;cursor:pointer;width:100%;font-size:16px;margin-top:10px}
button:hover{background:#45a049}
.info{background:#e3f2fd;padding:10px;border-radius:4px;margin:10px 0;font-size:14px}
.status{text-align:center;padding:10px;margin:10px 0;border-radius:4px}
.success{background:#c8e6c9;color:#2e7d32}
.error{background:#ffcdd2;color:#c62828}
.stats{background:#fff3e0;padding:10px;border-radius:4px;margin:10px 0;font-size:14px;text-align:center}
.random-active{background:#9c27b0 !important}
.random-inactive{background:#757575 !important}
</style>
</head>
<body>
<h1>ESP32-S3 Google Photos Frame</h1>

<div class='card'>
<h2>Google Photos Альбом</h2>
<label>URL альбома:</label>
<input type='text' id='albumUrl' placeholder='https://photos.app.goo.gl/...'>
<div class='info'>Вставьте ссылку на публичный альбом Google Photos</div>
<button onclick='saveAlbum()'>Сохранить альбом</button>
<div id='albumStats' class='stats' style='display:none'></div>
</div>

<div class='card'>
<h2>Режим показа</h2>
<label>Выберите режим:</label>
<select id='mode' onchange='toggleMode()'>
<option value='slideshow'>Слайд-шоу (автосмена)</option>
<option value='single'>Одно фото</option>
</select>

<div id='slideshowSettings'>
<label>Интервал смены (секунды):</label>
<input type='number' id='interval' min='1' max='3600' value='5'>

<label>Эффект перехода:</label>
<select id='effect'>
<option value='0'>Плавное затухание (Fade)</option>
<option value='1'>Слайд влево</option>
<option value='2'>Слайд вправо</option>
<option value='3'>Слайд вверх</option>
<option value='4'>Слайд вниз</option>
<option value='5'>Без эффекта (мгновенно)</option>
</select>
</div>

<div id='singlePhotoSettings' style='display:none'>
<label>Номер фото:</label>
<input type='number' id='photoIndex' min='0' value='0'>
<div class='info'>Укажите номер фото из альбома (начиная с 0)</div>
</div>

<button onclick='saveSettings()'>Применить настройки</button>
</div>

<div class='card'>
<h2>Управление</h2>
<button onclick='reloadPhotos()'>Обновить альбом</button>
<button id='randomBtn' class='random-inactive' onclick='toggleRandomMode()'>Случайный режим: ВЫКЛ</button>
<button onclick='nextPhoto()' style='background:#2196f3'>Следующее фото</button>
<button onclick='restartDevice()' style='background:#ff9800'>Перезагрузить устройство</button>
</div>

<div id='status'></div>

<script>
function loadSettings(){
  fetch('/getSettings').then(r=>r.json()).then(data=>{
    document.getElementById('albumUrl').value=data.albumUrl;
    document.getElementById('interval').value=data.interval;
    document.getElementById('photoIndex').value=data.photoIndex;
    document.getElementById('effect').value=data.effect;
    document.getElementById('mode').value=data.singleMode?'single':'slideshow';
    updateRandomButton(data.randomMode);
    toggleMode();
    
    if(data.totalPhotos>0){
      const stats=document.getElementById('albumStats');
      stats.style.display='block';
      stats.innerHTML='Всего фото: <b>'+data.totalPhotos+'</b> | Текущее: <b>'+(data.currentIndex+1)+'</b><br>'+
                      'В памяти: '+data.loadedInChunk+' фото (порция '+Math.floor(data.chunkStart/50+1)+')<br>'+
                      'Режим: '+(data.randomMode?'СЛУЧАЙНЫЙ':'по порядку');
    }
  });
}

function toggleMode(){
  const mode=document.getElementById('mode').value;
  document.getElementById('slideshowSettings').style.display=mode==='slideshow'?'block':'none';
  document.getElementById('singlePhotoSettings').style.display=mode==='single'?'block':'none';
}

function updateRandomButton(isActive){
  const btn=document.getElementById('randomBtn');
  if(isActive){
    btn.className='random-active';
    btn.innerHTML='Случайный режим: ВКЛ';
  }else{
    btn.className='random-inactive';
    btn.innerHTML='Случайный режим: ВЫКЛ';
  }
}

function toggleRandomMode(){
  fetch('/toggleRandom').then(r=>r.json()).then(data=>{
    if(data.success){
      updateRandomButton(data.randomMode);
      showStatus('Случайный режим: '+(data.randomMode?'ВКЛ':'ВЫКЛ'),true);
      setTimeout(loadSettings, 1000);
    }else{
      showStatus(data.message,false);
    }
  });
}

function nextPhoto(){
  fetch('/next').then(r=>r.json()).then(data=>{
    if(data.success){
      showStatus('Следующее фото: ' + (data.photoIndex + 1) + ' из ' + data.totalPhotos, true);
      setTimeout(loadSettings, 1000);
    }else{
      showStatus(data.message, false);
    }
  });
}

function showStatus(msg,success){
  const div=document.getElementById('status');
  div.innerHTML='<div class="status '+(success?'success':'error')+'">'+msg+'</div>';
  setTimeout(()=>div.innerHTML='',3000);
}

function saveAlbum(){
  const url=document.getElementById('albumUrl').value;
  fetch('/setAlbum',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({url:url})})
    .then(r=>r.json())
    .then(data=>{
      showStatus(data.message,data.success);
      if(data.success)setTimeout(()=>location.reload(),1500);
    });
}

function saveSettings(){
  const mode=document.getElementById('mode').value;
  const interval=parseInt(document.getElementById('interval').value);
  const photoIndex=parseInt(document.getElementById('photoIndex').value);
  const effect=parseInt(document.getElementById('effect').value);
  
  fetch('/setSettings',{method:'POST',headers:{'Content-Type':'application/json'},
    body:JSON.stringify({singleMode:mode==='single',interval:interval,photoIndex:photoIndex,effect:effect})})
    .then(r=>r.json())
    .then(data=>showStatus(data.message,data.success));
}

function reloadPhotos(){
  fetch('/reload').then(r=>r.json()).then(data=>showStatus(data.message,data.success));
}

function restartDevice(){
  if(confirm('Перезагрузить устройство?')){
    fetch('/restart').then(()=>showStatus('Устройство перезагружается...',true));
  }
}

window.onload=loadSettings;
</script>
</body>
</html>
)";

// ==================== ВЕБ-ОБРАБОТЧИКИ ====================

void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

void handleGetSettings() {
  StaticJsonDocument<512> doc;
  doc["albumUrl"] = albumUrl;
  doc["interval"] = slideshowInterval / 1000;
  doc["singleMode"] = singlePhotoMode;
  doc["photoIndex"] = selectedPhotoIndex;
  doc["effect"] = transitionEffect;
  doc["randomMode"] = randomMode;
  doc["totalPhotos"] = totalPhotosInAlbum;
  doc["currentIndex"] = currentPhotoIndex;
  doc["chunkStart"] = currentChunkStart;
  doc["loadedInChunk"] = photoUrls.size();
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleSetAlbum() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<512> doc;
    deserializeJson(doc, server.arg("plain"));
    
    String newUrl = doc["url"].as<String>();
    if (newUrl.length() > 0) {
      albumUrl = newUrl;
      preferences.putString("albumUrl", albumUrl);
      
      StaticJsonDocument<128> response;
      response["success"] = true;
      response["message"] = "Альбом сохранён! Загружаем...";
      
      String json;
      serializeJson(response, json);
      server.send(200, "application/json", json);
      
      delay(500);
      loadAlbumHTML();
      initializeRandomMode();
      loadPhotoChunk(0);
      if (photoUrls.size() > 0) {
        currentPhotoIndex = 0;
        loadCurrentPhoto();
      }
      return;
    }
  }
  
  StaticJsonDocument<128> response;
  response["success"] = false;
  response["message"] = "Ошибка сохранения";
  
  String json;
  serializeJson(response, json);
  server.send(400, "application/json", json);
}

void handleSetSettings() {
  if (server.hasArg("plain")) {
    StaticJsonDocument<256> doc;
    deserializeJson(doc, server.arg("plain"));
    
    bool newSingleMode = doc["singleMode"];
    int newInterval = doc["interval"];
    int newPhotoIndex = doc["photoIndex"];
    int newEffect = doc["effect"];
    
    singlePhotoMode = newSingleMode;
    slideshowInterval = newInterval * 1000;
    selectedPhotoIndex = newPhotoIndex;
    transitionEffect = newEffect;
    
    preferences.putBool("singleMode", singlePhotoMode);
    preferences.putULong("interval", slideshowInterval);
    preferences.putInt("photoIndex", selectedPhotoIndex);
    preferences.putInt("effect", transitionEffect);
    
    if (singlePhotoMode && selectedPhotoIndex < totalPhotosInAlbum) {
      // Загружаем нужную порцию если фото не в текущей
      int targetChunk = (selectedPhotoIndex / CHUNK_SIZE) * CHUNK_SIZE;
      if (targetChunk != currentChunkStart) {
        loadPhotoChunk(targetChunk);
      }
      currentPhotoIndex = selectedPhotoIndex;
      loadCurrentPhoto();
    }
    
    StaticJsonDocument<128> response;
    response["success"] = true;
    response["message"] = "Настройки сохранены!";
    
    String json;
    serializeJson(response, json);
  server.send(200, "application/json", json);
    return;
  }
  
  StaticJsonDocument<128> response;
  response["success"] = false;
  response["message"] = "Ошибка сохранения";
  
  String json;
  serializeJson(response, json);
  server.send(400, "application/json", json);
}

void handleReload() {
  loadAlbumHTML();
  initializeRandomMode();
  loadPhotoChunk(currentChunkStart);
  
  StaticJsonDocument<128> doc;
  doc["success"] = true;
  doc["message"] = "Альбом обновлён! Уникальных фото: " + String(totalPhotosInAlbum);
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleToggleRandom() {
  randomMode = !randomMode;
  preferences.putBool("randomMode", randomMode);
  
  if (randomMode) {
    initializeRandomMode();
  }
  
  StaticJsonDocument<128> doc;
  doc["success"] = true;
  doc["randomMode"] = randomMode;
  doc["message"] = randomMode ? "Случайный режим ВКЛ" : "Случайный режим ВЫКЛ";
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleNext() {
  if (totalPhotosInAlbum <= 2) {
    StaticJsonDocument<128> doc;
    doc["success"] = false;
    doc["message"] = "Недостаточно фото в альбоме";
    
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
    return;
  }

  if (randomMode) {
    getNextRandomPhoto();
  } else {
    currentPhotoIndex = (currentPhotoIndex + 1) % totalPhotosInAlbum;
    if (currentPhotoIndex < 2) currentPhotoIndex = 2;
  }
  
  // Загружаем нужную порцию если фото не в текущей
  int targetChunk = (currentPhotoIndex / CHUNK_SIZE) * CHUNK_SIZE;
  if (targetChunk != currentChunkStart) {
    loadPhotoChunk(targetChunk);
  }
  
  loadCurrentPhoto();

  StaticJsonDocument<128> doc;
  doc["success"] = true;
  doc["message"] = "Следующее фото загружено";
  doc["photoIndex"] = currentPhotoIndex;
  doc["totalPhotos"] = totalPhotosInAlbum;
  
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

void handleRestart() {
  server.send(200, "text/plain", "Restarting...");
  delay(1000);
  ESP.restart();
}

// ==================== ФУНКЦИИ РЕЖИМА СЛУЧАЙНОГО ПОКАЗА ====================

// Инициализация списка доступных фото для случайного показа
void initializeRandomMode() {
  availablePhotoIndices.clear();
  
  if (totalPhotosInAlbum <= 2) {
    return;
  }
  
  // Заполняем список индексами от 2 до totalPhotosInAlbum-1
  for (int i = 2; i < totalPhotosInAlbum; i++) {
    availablePhotoIndices.push_back(i);
  }
  
  // Перемешиваем список
  for (int i = 0; i < availablePhotoIndices.size(); i++) {
    int randomIndex = random(0, availablePhotoIndices.size());
    int temp = availablePhotoIndices[i];
    availablePhotoIndices[i] = availablePhotoIndices[randomIndex];
    availablePhotoIndices[randomIndex] = temp;
  }
  
  currentRandomIndex = 0;
  Serial.printf("🎲 Инициализирован случайный режим: %d фото\n", availablePhotoIndices.size());
}

// Получить следующее случайное фото
void getNextRandomPhoto() {
  if (availablePhotoIndices.size() == 0) {
    // Если список пуст, переинициализируем
    initializeRandomMode();
  }
  
  if (availablePhotoIndices.size() > 0) {
    currentPhotoIndex = availablePhotoIndices[currentRandomIndex];
    currentRandomIndex++;
    
    // Если дошли до конца списка, перемешиваем заново
    if (currentRandomIndex >= availablePhotoIndices.size()) {
      // Перемешиваем текущий список
      for (int i = 0; i < availablePhotoIndices.size(); i++) {
        int randomIndex = random(0, availablePhotoIndices.size());
        int temp = availablePhotoIndices[i];
        availablePhotoIndices[i] = availablePhotoIndices[randomIndex];
        availablePhotoIndices[randomIndex] = temp;
      }
      currentRandomIndex = 0;
      Serial.println("🎲 Перемешиваем список фото заново");
    }
  }
}

// ==================== ФУНКЦИИ РАБОТЫ С ФОТО ====================

// Функция отрисовки для TJpg_Decoder на дисплей
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= gfx->height()) return 0;
  gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
  return 1;
}

// Функция отрисовки в буфер
bool buffer_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  int screenW = gfx->width();
  int screenH = gfx->height();
  if (y >= screenH) return 0;

  for (int row = 0; row < h; row++) {
    if (y + row < screenH) {
      memcpy(&nextPhotoBuffer[(y + row) * screenW + x], &bitmap[row * w], w * 2);
    }
  }
  return 1;
}

// Подсчитать общее количество фото в HTML
int countPhotosInHTML(String html) {
  std::vector<String> uniqueUrls;
  int startIndex = 0;

  while ((startIndex = html.indexOf("https://lh3.googleusercontent.com/", startIndex)) != -1) {
    int endIndex = html.indexOf("\"", startIndex);
    if (endIndex == -1) break;
    
    String photoUrl = html.substring(startIndex, endIndex);
    
    // Очищаем URL от параметров
    int eqIndex = photoUrl.indexOf("=");
    if (eqIndex != -1) {
      photoUrl = photoUrl.substring(0, eqIndex);
    }
    
    // Проверяем уникальность URL
    bool isUnique = true;
    for (const String& existingUrl : uniqueUrls) {
      if (existingUrl == photoUrl) {
        isUnique = false;
        break;
      }
    }
    
    if (isUnique) {
      uniqueUrls.push_back(photoUrl);
    }
    
    startIndex = endIndex + 1;
  }

  Serial.printf("🔍 Найдено уникальных фото: %d\n", uniqueUrls.size());
  return uniqueUrls.size();
}

// Загрузить HTML альбома
void loadAlbumHTML() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi не подключен");
    return;
  }

  Serial.println("🌐 Загружаем HTML альбома...");
  HTTPClient http;
  http.begin(albumUrl);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(15000);

  int httpCode = http.GET();
  if (httpCode == 200) {
    cachedHTML = http.getString();
    totalPhotosInAlbum = countPhotosInHTML(cachedHTML);
    Serial.printf("✅ Всего фото в альбоме: %d\n", totalPhotosInAlbum);
  } else {
    Serial.printf("❌ Ошибка загрузки: %d\n", httpCode);
  }
  http.end();
}

// Загрузить порцию URLs
void loadPhotoChunk(int startIndex) {
  photoUrls.clear();
  
  if (cachedHTML.length() == 0) {
    Serial.println("❌ HTML не загружен");
    return;
  }

  // Сначала собираем все уникальные URL
  std::vector<String> allUniqueUrls;
  int htmlIndex = 0;
  
  while ((htmlIndex = cachedHTML.indexOf("https://lh3.googleusercontent.com/", htmlIndex)) != -1) {
    int endIndex = cachedHTML.indexOf("\"", htmlIndex);
    if (endIndex == -1) break;
    
    String photoUrl = cachedHTML.substring(htmlIndex, endIndex);
    int eqIndex = photoUrl.indexOf("=");
    if (eqIndex != -1) {
      photoUrl = photoUrl.substring(0, eqIndex);
    }
    
    // Проверяем уникальность
    bool isUnique = true;
    for (const String& existingUrl : allUniqueUrls) {
      if (existingUrl == photoUrl) {
        isUnique = false;
        break;
      }
    }
    
    if (isUnique) {
      allUniqueUrls.push_back(photoUrl);
    }
    
    htmlIndex = endIndex + 1;
  }

  totalPhotosInAlbum = allUniqueUrls.size();
  
  if (totalPhotosInAlbum == 0) {
    Serial.println("❌ Не найдено фото в альбоме");
    return;
  }

  int screenW = gfx->width();
  int screenH = gfx->height();
  int foundCount = 0;

  Serial.printf("📦 Загружаем порцию фото %d-%d из %d...\n", 
                startIndex, startIndex + CHUNK_SIZE - 1, totalPhotosInAlbum);

  // Загружаем CHUNK_SIZE фото начиная с startIndex
  for (int i = startIndex; i < totalPhotosInAlbum && foundCount < CHUNK_SIZE; i++) {
    String photoUrl = allUniqueUrls[i];
    photoUrl += "=w" + String(screenW) + "-h" + String(screenH) + "-p";
    photoUrls.push_back(photoUrl);
    foundCount++;
  }

  currentChunkStart = startIndex;
  Serial.printf("✅ Загружено %d URLs в память (порция с %d)\n", photoUrls.size(), currentChunkStart);
}

// Получить URL по глобальному индексу
String getPhotoUrl(int globalIndex) {
  int localIndex = globalIndex - currentChunkStart;
  
  // Проверяем, в текущей ли порции
  if (localIndex >= 0 && localIndex < photoUrls.size()) {
    return photoUrls[localIndex];
  }

  // Нужно загрузить другую порцию
  int newChunkStart = (globalIndex / CHUNK_SIZE) * CHUNK_SIZE;
  Serial.printf("🔄 Переключение на порцию %d\n", newChunkStart / CHUNK_SIZE + 1);
  loadPhotoChunk(newChunkStart);
  
  localIndex = globalIndex - currentChunkStart;
  if (localIndex >= 0 && localIndex < photoUrls.size()) {
    return photoUrls[localIndex];
  }

  return "";
}

// Быстрая проверка доступности фото (HEAD запрос)
bool checkPhotoAvailable(String url) {
  HTTPClient http;
  http.begin(url);
  http.setTimeout(5000); // 5 секунд для проверки
  
  int httpCode = http.sendRequest("HEAD");
  http.end();
  
  bool available = (httpCode == 200);
  if (!available) {
    Serial.printf("❌ Фото недоступно, код: %d\n", httpCode);
  }
  return available;
}

// Загрузить фото по URL с таймаутом
bool loadPhotoToBuffer(String url, int timeout_ms = 15000) {
  int screenW = gfx->width();
  int screenH = gfx->height();

  if (nextPhotoBuffer != nullptr) {
    free(nextPhotoBuffer);
    nextPhotoBuffer = nullptr;
  }

  nextPhotoBuffer = (uint16_t*)ps_malloc(screenW * screenH * 2);
  if (!nextPhotoBuffer) {
    Serial.println("❌ Не удалось выделить память для буфера фото");
    return false;
  }
  memset(nextPhotoBuffer, 0, screenW * screenH * 2);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(timeout_ms);

  unsigned long startTime = millis();
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    int contentLength = http.getSize();
    if (contentLength <= 0) {
      Serial.println("❌ Неизвестный размер файла");
      http.end();
      free(nextPhotoBuffer);
      nextPhotoBuffer = nullptr;
      return false;
    }

    if (jpegBuffer != nullptr) free(jpegBuffer);
    jpegBuffer = (uint8_t*)ps_malloc(contentLength);
    if (!jpegBuffer) {
      Serial.println("❌ Не удалось выделить память для JPEG");
      http.end();
      free(nextPhotoBuffer);
      nextPhotoBuffer = nullptr;
      return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    size_t bytesRead = 0;
    
    while (http.connected() && bytesRead < contentLength) {
      // Проверка таймаута
      if (millis() - startTime > timeout_ms) {
        Serial.println("❌ Таймаут загрузки фото");
        break;
      }
      
      size_t available = stream->available();
      if (available) {
        int c = stream->readBytes(&jpegBuffer[bytesRead], available);
        bytesRead += c;
      }
      delay(1);
    }

    if (bytesRead == contentLength) {
      TJpgDec.setJpgScale(1);
      TJpgDec.setCallback(buffer_output);
      JRESULT result = TJpgDec.drawJpg(0, 0, jpegBuffer, bytesRead);

      if (result == JDR_OK) {
        Serial.println("✅ Фото декодировано");
        http.end();
        free(jpegBuffer);
        jpegBuffer = nullptr;
        return true;
      } else {
        Serial.printf("❌ Ошибка декодирования: %d\n", result);
      }
    } else {
      Serial.printf("❌ Загружено не полностью: %d/%d байт\n", bytesRead, contentLength);
    }
  } else {
    Serial.printf("❌ Ошибка HTTP: %d\n", httpCode);
  }

  http.end();
  if (jpegBuffer != nullptr) { 
    free(jpegBuffer); 
    jpegBuffer = nullptr; 
  }
  if (nextPhotoBuffer != nullptr) {
    free(nextPhotoBuffer);
    nextPhotoBuffer = nullptr;
  }
  return false;
}

// Эффекты перехода между фото
void fadeToNextPhoto() {
  int screenW = gfx->width();
  int screenH = gfx->height();

  if (!nextPhotoBuffer) return;

  if (currentPhotoBuffer == nullptr) {
    currentPhotoBuffer = (uint16_t*)ps_malloc(screenW * screenH * 2);
    if (!currentPhotoBuffer) return;
  }

  // Применяем эффект перехода
  switch (transitionEffect) {
    case 0: // Плавное затухание (Fade) - ОПТИМИЗИРОВАННАЯ ВЕРСИЯ
      {
        const int FADE_STEPS = 25;  // Оптимальное количество шагов
        const int FADE_DELAY = 10;  // Базовая задержка
        
        // Используем временный буфер для вычислений
        uint16_t* fadeBuffer = (uint16_t*)ps_malloc(screenW * screenH * 2);
        if (!fadeBuffer) {
          gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
          break;
        }

        for (int alpha = 0; alpha <= FADE_STEPS; alpha++) {
          unsigned long stepStart = millis();
          
          // Оптимизированный цикл с предварительными вычислениям
          float progress = (float)alpha / FADE_STEPS;
          
          for (int i = 0; i < screenW * screenH; i++) {
            uint16_t oldPixel = currentPhotoBuffer[i];
            uint16_t newPixel = nextPhotoBuffer[i];
            
            // Извлекаем RGB компоненты (RGB565)
            uint8_t r1 = (oldPixel >> 11) & 0x1F;
            uint8_t g1 = (oldPixel >> 5) & 0x3F;
            uint8_t b1 = oldPixel & 0x1F;
            
            uint8_t r2 = (newPixel >> 11) & 0x1F;
            uint8_t g2 = (newPixel >> 5) & 0x3F;
            uint8_t b2 = newPixel & 0x1F;
            
            // Интерполяция с плавающей точкой для плавности
            uint8_t r = r1 + (uint8_t)((r2 - r1) * progress);
            uint8_t g = g1 + (uint8_t)((g2 - g1) * progress);
            uint8_t b = b1 + (uint8_t)((b2 - b1) * progress);
            
            fadeBuffer[i] = (r << 11) | (g << 5) | b;
          }
          
          gfx->draw16bitRGBBitmap(0, 0, fadeBuffer, screenW, screenH);
          
          // Динамическая задержка для равномерной анимации
          unsigned long stepTime = millis() - stepStart;
          if (stepTime < FADE_DELAY) {
            delay(FADE_DELAY - stepTime);
          }
          
          // Последний шаг - убеждаемся, что показываем финальное изображение
          if (alpha == FADE_STEPS) {
            // Копируем финальное изображение напрямую
            memcpy(currentPhotoBuffer, nextPhotoBuffer, screenW * screenH * 2);
            gfx->draw16bitRGBBitmap(0, 0, currentPhotoBuffer, screenW, screenH);
          }
        }
        
        free(fadeBuffer);
      }
      break;
      
    case 1: // Слайд влево - ОПТИМИЗИРОВАННАЯ ВЕРСИЯ
      {
        uint16_t* tempBuffer = (uint16_t*)ps_malloc(screenW * screenH * 2);
        if (!tempBuffer) {
          gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
          break;
        }
        
        const int STEP_SIZE = 15; // Увеличиваем шаг для более быстрой анимации
        for (int offset = 0; offset <= screenW; offset += STEP_SIZE) {
          unsigned long stepStart = millis();
          
          for (int y = 0; y < screenH; y++) {
            // Часть старого фото слева
            if (offset < screenW) {
              memcpy(&tempBuffer[y * screenW], 
                     &currentPhotoBuffer[y * screenW + offset], 
                     (screenW - offset) * 2);
            }
            // Часть нового фото справа
            if (offset > 0) {
              memcpy(&tempBuffer[y * screenW + (screenW - offset)], 
                     &nextPhotoBuffer[y * screenW], 
                     offset * 2);
            }
          }
          gfx->draw16bitRGBBitmap(0, 0, tempBuffer, screenW, screenH);
          
          // Динамическая задержка
          unsigned long stepTime = millis() - stepStart;
          if (stepTime < 10) {
            delay(10 - stepTime);
          }
        }
        
        // Финальное изображение
        gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
        free(tempBuffer);
      }
      break;
      
    case 2: // Слайд вправо - ОПТИМИЗИРОВАННАЯ ВЕРСИЯ
      {
        uint16_t* tempBuffer = (uint16_t*)ps_malloc(screenW * screenH * 2);
        if (!tempBuffer) {
          gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
          break;
        }
        
        const int STEP_SIZE = 15;
        for (int offset = 0; offset <= screenW; offset += STEP_SIZE) {
          unsigned long stepStart = millis();
          
          for (int y = 0; y < screenH; y++) {
            // Часть нового фото слева
            if (offset > 0) {
              memcpy(&tempBuffer[y * screenW], 
                     &nextPhotoBuffer[y * screenW + (screenW - offset)], 
                     offset * 2);
            }
            // Часть старого фото справа
            if (offset < screenW) {
              memcpy(&tempBuffer[y * screenW + offset], 
                     &currentPhotoBuffer[y * screenW], 
                     (screenW - offset) * 2);
            }
          }
          gfx->draw16bitRGBBitmap(0, 0, tempBuffer, screenW, screenH);
          
          unsigned long stepTime = millis() - stepStart;
          if (stepTime < 10) {
            delay(10 - stepTime);
          }
        }
        
        gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
        free(tempBuffer);
      }
      break;
      
    case 3: // Слайд вверх - ОПТИМИЗИРОВАННАЯ ВЕРСИЯ
      {
        uint16_t* tempBuffer = (uint16_t*)ps_malloc(screenW * screenH * 2);
        if (!tempBuffer) {
          gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
          break;
        }
        
        const int STEP_SIZE = 15;
        for (int offset = 0; offset <= screenH; offset += STEP_SIZE) {
          unsigned long stepStart = millis();
          
          // Старое фото сверху
          if (offset < screenH) {
            memcpy(tempBuffer, 
                   &currentPhotoBuffer[offset * screenW], 
                   (screenH - offset) * screenW * 2);
          }
          // Новое фото снизу
          if (offset > 0) {
            memcpy(&tempBuffer[(screenH - offset) * screenW], 
                   nextPhotoBuffer, 
                   offset * screenW * 2);
          }
          gfx->draw16bitRGBBitmap(0, 0, tempBuffer, screenW, screenH);
          
          unsigned long stepTime = millis() - stepStart;
          if (stepTime < 10) {
            delay(10 - stepTime);
          }
        }
        
        gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
        free(tempBuffer);
      }
      break;
      
    case 4: // Слайд вниз - ОПТИМИЗИРОВАННАЯ ВЕРСИЯ
      {
        uint16_t* tempBuffer = (uint16_t*)ps_malloc(screenW * screenH * 2);
        if (!tempBuffer) {
          gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
          break;
        }
        
        const int STEP_SIZE = 15;
        for (int offset = 0; offset <= screenH; offset += STEP_SIZE) {
          unsigned long stepStart = millis();
          
          // Новое фото сверху
          if (offset > 0) {
            memcpy(tempBuffer, 
                   &nextPhotoBuffer[(screenH - offset) * screenW], 
                   offset * screenW * 2);
          }
          // Старое фото снизу
          if (offset < screenH) {
            memcpy(&tempBuffer[offset * screenW], 
                   currentPhotoBuffer, 
                   (screenH - offset) * screenW * 2);
          }
          gfx->draw16bitRGBBitmap(0, 0, tempBuffer, screenW, screenH);
          
          unsigned long stepTime = millis() - stepStart;
          if (stepTime < 10) {
            delay(10 - stepTime);
          }
        }
        
        gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
        free(tempBuffer);
      }
      break;
      
    case 5: // Без эффекта
    default:
      gfx->draw16bitRGBBitmap(0, 0, nextPhotoBuffer, screenW, screenH);
      break;
  }

  // Всегда обновляем currentPhotoBuffer
  if (currentPhotoBuffer && nextPhotoBuffer) {
    memcpy(currentPhotoBuffer, nextPhotoBuffer, screenW * screenH * 2);
  }
  nextPhotoLoaded = false;

  Serial.printf("✅ Фото %d/%d отображено (порция %d) %s\n", 
                currentPhotoIndex + 1, totalPhotosInAlbum, currentChunkStart / CHUNK_SIZE + 1,
                randomMode ? "🎲" : "");
}

void loadCurrentPhoto() {
  if (totalPhotosInAlbum == 0) return;
  
  String photoUrl = getPhotoUrl(currentPhotoIndex);
  if (photoUrl.length() > 0) {
    Serial.printf("📥 Загрузка фото %d/%d %s\n", currentPhotoIndex + 1, totalPhotosInAlbum, randomMode ? "🎲" : "");
    if (loadPhotoToBuffer(photoUrl)) {
      fadeToNextPhoto();
      failedLoadAttempts = 0; // Сбрасываем счетчик при успешной загрузке
    } else {
      failedLoadAttempts++;
      Serial.printf("⚠️ Ошибка загрузки (%d попытка)\n", failedLoadAttempts);
      
      if (failedLoadAttempts >= MAX_FAILED_ATTEMPTS) {
        Serial.println("🚫 Пропускаем проблемное фото");
        if (randomMode) {
          getNextRandomPhoto();
        } else {
          currentPhotoIndex = (currentPhotoIndex + 1) % totalPhotosInAlbum;
          if (currentPhotoIndex < 2) currentPhotoIndex = 2;
        }
        failedLoadAttempts = 0;
      }
    }
  }
}

void preloadNextPhoto() {
  if (totalPhotosInAlbum < 3) return;

  // Если уже есть предзагруженное фото, не загружаем новое
  if (nextPhotoLoaded) return;

  int nextIndex;
  if (randomMode) {
    // Для случайного режима предзагружаем следующее случайное фото
    if (availablePhotoIndices.size() > 0) {
      int nextRandomIndex = (currentRandomIndex) % availablePhotoIndices.size();
      nextIndex = availablePhotoIndices[nextRandomIndex];
    } else {
      return;
    }
  } else {
    nextIndex = (currentPhotoIndex + 1) % totalPhotosInAlbum;
    if (nextIndex < 2) nextIndex = 2;
  }

  // Пропускаем если пытаемся загрузить то же самое фото
  if (nextIndex == currentPhotoIndex) return;

  String photoUrl = getPhotoUrl(nextIndex);

  if (photoUrl.length() > 0) {
    Serial.printf("📥 Предзагрузка фото %d/%d %s\n", nextIndex + 1, totalPhotosInAlbum, randomMode ? "🎲" : "");
    
    // Быстрая проверка доступности фото
    if (checkPhotoAvailable(photoUrl)) {
      if (loadPhotoToBuffer(photoUrl, 10000)) { // Таймаут 10 секунд для предзагрузки
        nextPhotoLoaded = true;
        Serial.println("✅ Предзагрузка успешна");
      } else {
        Serial.println("❌ Предзагрузка не удалась, пропускаем");
        nextPhotoLoaded = false;
      }
    } else {
      Serial.println("❌ Фото недоступно, пропускаем");
      nextPhotoLoaded = false;
    }
  }
}

// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);
  Serial.println("🖼️ ESP32-S3 Google Photos Frame (Arduino GFX версия)");

  // Инициализация генератора случайных чисел
  randomSeed(analogRead(0));

  // Загружаем настройки
  preferences.begin("photoframe", false);

  albumUrl = preferences.getString("albumUrl", "");   // БЕЗ дефолтной ссылки
  slideshowInterval = preferences.getULong("interval", 5000);
  singlePhotoMode = preferences.getBool("singleMode", false);
  selectedPhotoIndex = preferences.getInt("photoIndex", 2);
  transitionEffect = preferences.getInt("effect", 0);
  randomMode = preferences.getBool("randomMode", false);

  currentPhotoIndex = singlePhotoMode ? selectedPhotoIndex : 2;
  if (currentPhotoIndex < 2) currentPhotoIndex = 2;

  // Инициализация дисплея
  gfx->begin();
  // gfx->setRotation(TFT_ROTATION);
  gfx->fillScreen(0x0000); // Черный фон

  // Подсветка
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BRIGHTNESS);

  TJpgDec.setCallback(tft_output);
  TJpgDec.setJpgScale(1);

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("📡 Подключение к WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n✅ WiFi подключен: %s\n", WiFi.localIP().toString().c_str());

  // Показать IP на экране
  gfx->setTextColor(0xFFFF); // Белый
  gfx->setTextSize(2);
  gfx->setCursor(10, gfx->height() / 2 - 20);
  gfx->print("IP: " + WiFi.localIP().toString());
  gfx->setTextSize(2);
  gfx->setCursor(10, gfx->height() / 2 + 10);
  gfx->print("open web ip");
  delay(6000);
  gfx->fillScreen(0x0000);

  // Веб-сервер
  server.on("/", handleRoot);
  server.on("/getSettings", handleGetSettings);
  server.on("/setAlbum", HTTP_POST, handleSetAlbum);
  server.on("/setSettings", HTTP_POST, handleSetSettings);
  server.on("/reload", handleReload);
  server.on("/toggleRandom", handleToggleRandom);
  server.on("/next", handleNext);
  server.on("/restart", handleRestart);
  server.begin();
  Serial.println("🌐 Веб-сервер запущен");

  // Если URL не задан — просто ждём
  if (albumUrl.length() == 0) {
    gfx->fillScreen(0x0000);
    gfx->setTextSize(2);
    gfx->setTextColor(0xFFFF);
    gfx->setCursor(20, gfx->height() / 2 - 20);
    gfx->print("URL not find");
    gfx->setTextSize(2);
    gfx->setCursor(20, gfx->height() / 2 + 10);
    gfx->print("Open the web interface");
    Serial.println("⚠ URL альбома не задан — ожидаем");
    return;
  }

  // Загружаем альбом
  loadAlbumHTML();
  initializeRandomMode();

  if (totalPhotosInAlbum > 2) {
    int startChunk = (currentPhotoIndex / CHUNK_SIZE) * CHUNK_SIZE;
    loadPhotoChunk(startChunk);
    loadCurrentPhoto();
    preloadNextPhoto();
  } else {
    gfx->setTextColor(0xFFFF);
    gfx->setCursor(30, gfx->height() / 2);
    gfx->print("Not enough photo (>2)");
  }
}

// ==================== LOOP ====================

void loop() {
  server.handleClient();

  if (!singlePhotoMode && totalPhotosInAlbum > 2) {
    if (millis() - lastChange > slideshowInterval) {
      
      if (nextPhotoLoaded) {
        // Есть предзагруженное фото - показываем его
        fadeToNextPhoto();
        
        // Выбираем следующее фото в зависимости от режима
        if (randomMode) {
          getNextRandomPhoto();
        } else {
          currentPhotoIndex = (currentPhotoIndex + 1) % totalPhotosInAlbum;
          if (currentPhotoIndex < 2) currentPhotoIndex = 2;
        }
        
        lastChange = millis();
        nextPhotoLoaded = false; // Сбрасываем флаг
        preloadNextPhoto(); // Начинаем загрузку следующего
      } else {
        // Нет предзагруженного фото - пытаемся загрузить текущее напрямую
        Serial.println("⚠️ Предзагрузка не успела, загружаем напрямую");
        int nextIndex;
        if (randomMode) {
          getNextRandomPhoto();
          nextIndex = currentPhotoIndex;
        } else {
          nextIndex = (currentPhotoIndex + 1) % totalPhotosInAlbum;
          if (nextIndex < 2) nextIndex = 2;
        }
        
        String photoUrl = getPhotoUrl(nextIndex);
        if (photoUrl.length() > 0 && loadPhotoToBuffer(photoUrl, 15000)) {
          fadeToNextPhoto();
          currentPhotoIndex = nextIndex;
          lastChange = millis();
        } else {
          // Если загрузка не удалась, пропускаем это фото
          Serial.println("❌ Пропускаем фото из-за ошибки загрузки");
          if (randomMode) {
            getNextRandomPhoto();
          } else {
            currentPhotoIndex = nextIndex;
          }
          lastChange = millis() - slideshowInterval + 2000; // Ждем 2 секунды перед следующей попыткой
        }
        
        // Пытаемся предзагрузить следующее
        preloadNextPhoto();
      }
    }
  }

  // Автообновление альбома каждые 10 минут
  static unsigned long lastReload = 0;
  if (millis() - lastReload > 600000) {
    loadAlbumHTML();
    initializeRandomMode();
    loadPhotoChunk(currentChunkStart);
    lastReload = millis();
  }

  delay(10); // Небольшая задержка для стабильности
}