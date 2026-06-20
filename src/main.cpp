// ============================================================
//  ESP32-S2 Mini + ST7789 76x284 (paisagem 284x76)
//  Radio FM SI4703 - FIRMWARE (UI + navegacao)
// ------------------------------------------------------------
//  Hardware (sugestao de pinos):
//    Display ST7789 : MOSI35 SCLK36 MISO40 CS34 DC37 RST38
//    Teclado 1x4    : COM=GND ; teclas 1..4 = GPIO1 GPIO2 GPIO3 GPIO4 (pullup)
//    SI4703 (I2C)   : SDA=GPIO8 SCL=GPIO9 RST=GPIO7
//    Bateria LiPo   : GPIO6 (ADC1) via divisor 1:2 da celula (ver README)
//
//  A radio esta SIMULADA (modelo em RAM). Os pontos de
//  integracao com o SI4703 estao marcados com TODO[SI4703].
//
//  Teste sem hardware: comandos pelo Serial ->
//    '1'..'4' = clique curto ;  'q' 'w' 'e' 'r' = clique longo
// ============================================================

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <Preferences.h>
#include <radio.h>
#include <SI4703.h>
#include "RDSParserLocal.h"   // copia local (descodificacao de RadioText melhorada)

// fontes smooth (anti-aliased VLW, embebidas em FLASH) - contornos suavizados
#include "NotoSansBold15.h"
#include "Latin_Hiragana_24.h"
#define FF_BIG   (Latin_Hiragana_24)   // frequencia / nome estacao / volume (24px)
#define FF_TITLE (NotoSansBold15)      // titulos dos ecras (15px, bold)

// 1 = usa o SI4703 real ; 0 = modelo simulado em RAM
#define USE_SI4703 1

// 1 = le a carga da LiPo (divisor resistivo no ADC) ; 0 = desliga o indicador
#define USE_BATTERY 1

// pinos do SI4703 (I2C + reset)
static const uint8_t PIN_SDA = 8, PIN_SCL = 9, PIN_RST = 7;

// medicao da bateria: GPIO6 = ADC1, le o ponto medio de um divisor 1:2 (R1=R2)
// ligado a celula LiPo (B+). Vbat = 2 x Vadc. Ver esquema no README.
static const uint8_t PIN_VBAT = 6;
static const float   VBAT_DIV = 2.0f;     // razao do divisor (R1=R2=100k)

SI4703 radio;
RDSParserLocal rds;

TFT_eSPI tft = TFT_eSPI();

// --- Painel fisico dentro da RAM 240x320 do controlador ---
static const int16_t PW = 76;
static const int16_t PH = 284;
static const int16_t OX = 82;
static const int16_t OY = 18;

// --- Tela logica (paisagem) ---
static const int16_t LW = 284;
static const int16_t LH = 76;

TFT_eSprite cv = TFT_eSprite(&tft);

// --- Teclado de membrana 1x4 ---
// Tira unica com 5 pinos: 1 linha comum (COM) + 4 teclas. Cada tecla liga a
// sua linha ao COM. O COM liga DIRETAMENTE a GND (NAO a um GPIO nem a 3V3/5V) e
// cada tecla e' lida com pull-up interno (premir -> pino a LOW).
// (Antes o COM era conduzido pelo GPIO5; passou a GND para evitar que um pino
//  ficasse em curto com a alimentacao em caso de erro de ligacao.)
static const uint8_t BTN_PIN[4] = {2, 1, 4, 3}; // teclas 1..4

// --- Paleta (RGB565) ---
#define COL_BG      TFT_BLACK
#define COL_CYAN    0x07FF
#define COL_BLUE    0x051F
#define COL_GREEN   0x07E0
#define COL_ORANGE  0xFD20
#define COL_WHITE   0xFFFF
#define COL_GREY    0x8410
#define COL_DKGREY  0x39E7
#define COL_FRAME   0x10A2

// =================== ESTADO DA RADIO =======================
enum Screen { SC_MAIN, SC_TUNE, SC_VOLUME, SC_PRESETS, SC_SCAN, SC_MENU, SC_MSG };

struct Preset { float f; char name[12]; };
const int MAX_PRESET = 20;
Preset presets[MAX_PRESET];
int nPresets = 0;             // sem memorias por defeito

// limites da banda FM (MHz)
const float FM_MIN = 87.5f, FM_MAX = 108.0f;

struct State {
  Screen screen = SC_MAIN;
  float  freq   = FM_MIN;
  float  step   = 0.10f;
  int    volume = 14;
  bool   muted  = false;
  bool   stereo = false;
  int    rssi   = 0;          // 0..5
  bool   hasRDS = false;
  String station = "";
  String radiotext = "";
  int    presetSel = 0;
  int    menuSel   = 0;
  bool   scanning  = false;
  // bateria (LiPo)
  bool   battValid = false;   // ja foi feita uma leitura valida
  int    battPct   = 0;       // 0..100 (estimativa)
  float  battV     = 0.0f;    // tensao da celula (V)
  // mensagem
  String msgTitle, msgL1, msgL2;
  bool   msgCheck = false;
  uint32_t msgUntil = 0;
  Screen msgReturn = SC_MAIN;
} st;

static String f2(float f) { char b[8]; snprintf(b, sizeof(b), "%.2f", f); return String(b); }

// associa frequencia a um preset (define estacao / RDS)
// devolve o indice do preset com a frequencia atual, ou -1
static int currentPreset() {
  for (int i = 0; i < nPresets; i++)
    if (fabs(presets[i].f - st.freq) < 0.05f) return i;
  return -1;
}

// guarda uma memoria (apend, ate ao maximo)
static bool storePreset(float f, const String& name) {
  if (nPresets >= MAX_PRESET) return false;
  presets[nPresets].f = f;
  String n = name.length() ? name : String("----");
  n.toCharArray(presets[nPresets].name, sizeof(presets[nPresets].name));
  nPresets++;
  return true;
}

// remove uma memoria (desloca as seguintes para tras)
static void removePreset(int idx) {
  if (idx < 0 || idx >= nPresets) return;
  for (int i = idx; i < nPresets - 1; i++) presets[i] = presets[i + 1];
  nPresets--;
  if (st.presetSel >= nPresets) st.presetSel = (nPresets > 0) ? nPresets - 1 : 0;
}

// ---- persistencia das memorias em NVS ("EEPROM" do ESP32) ----
Preferences prefs;
static void savePresetsNVS() {
  prefs.begin("fmradio", false);
  prefs.putInt("n", nPresets);
  prefs.putBytes("presets", presets, sizeof(Preset) * nPresets);
  prefs.end();
}
static void loadPresetsNVS() {
  prefs.begin("fmradio", true);
  nPresets = prefs.getInt("n", 0);
  if (nPresets < 0) nPresets = 0;
  if (nPresets > MAX_PRESET) nPresets = MAX_PRESET;
  if (nPresets > 0) prefs.getBytes("presets", presets, sizeof(Preset) * nPresets);
  prefs.end();
}

// ---- ultima sintonia/volume (restaurado ao ligar) ----
static void saveSettingsNVS() {
  prefs.begin("fmradio", false);
  prefs.putUShort("freq", (uint16_t)lroundf(st.freq * 100));
  prefs.putUChar("vol", (uint8_t)st.volume);
  prefs.putBool("mute", st.muted);
  prefs.end();
}
static void loadSettingsNVS() {
  prefs.begin("fmradio", true);
  uint16_t f = prefs.getUShort("freq", 0);
  st.volume  = prefs.getUChar("vol", st.volume);
  st.muted   = prefs.getBool("mute", false);
  prefs.end();
  if (f >= 8750 && f <= 10800) st.freq = f / 100.0f;
}

// gravacao atrasada das definicoes (evita escritas a cada clique)
static bool     settingsDirty = false;
static uint32_t settingsDirtyAt = 0;
static void markSettingsDirty() { settingsDirty = true; settingsDirtyAt = millis(); }

// --- callbacks de RDS (SI4703) ---
static void rdsServiceName(const char* name) {
  st.station = name;
  st.station.trim();
  if (st.station.length()) st.hasRDS = true;
}
// A RDSParserLocal so chama este callback quando a mensagem de radiotext esta
// COMPLETA (ver RDSParserLocal.cpp), por isso basta sanitizar e mostrar.
// Sanitizamos caracteres nao-imprimiveis (-> espaco) e ignoramos texto vazio.
static void rdsTextCb(const char* text) {
  String t;
  for (const char* p = text; *p && (p - text) < 64; ++p) {
    t += (*p >= 0x20 && *p < 0x7F) ? *p : ' ';
  }
  t.trim();
  if (!t.length()) return;                          // ignora vazio (reset/erros)
  if (t != st.radiotext) st.radiotext = t;
}
static void rdsProcess(uint16_t b1, uint16_t b2, uint16_t b3, uint16_t b4) {
  rds.processData(b1, b2, b3, b4);
}

#if USE_SI4703
// Leitura direta do RDS no SI4703 (registos 0x0A..0x0F), em vez de radio.checkRDS().
//
// Porque: o checkRDS() da biblioteca so entrega o grupo se TODOS os 4 blocos
// forem corrigiveis. No RadioText os caracteres estao nos blocos C e D e o
// indice no bloco B; o bloco A (codigo PI) NAO e preciso. Bastava um erro
// irrecuperavel no bloco A para descartar o grupo inteiro e perder texto bom,
// fazendo a mensagem nunca completar. Aqui ignoramos erros do bloco A.
//
// Bonus: RSSI/estereo saem do mesmo registo 0x0A, evitando uma leitura extra
// (getRadioInfo) que limpava o flag RDSR e tambem fazia perder grupos.
static const uint8_t SI4703_ADDR = 0x10;
static uint32_t      lastRdsPoll = 0;

static void pollSI4703RDS() {
  uint32_t now = millis();
  if (now - lastRdsPoll < 35) return;          // grupos chegam ~a cada 87ms
  lastRdsPoll = now;

  // le 0x0A..0x0F (status + RDS) numa so transacao (6 registos = 12 bytes)
  if (Wire.requestFrom((int)SI4703_ADDR, 12) != 12) return;
  uint16_t reg[6];
  for (int i = 0; i < 6; i++) {
    uint8_t hi = Wire.read();
    uint8_t lo = Wire.read();
    reg[i] = ((uint16_t)hi << 8) | lo;
  }
  uint16_t statusRssi = reg[0];                // 0x0A
  uint16_t readChan   = reg[1];                // 0x0B

  // RSSI (bits 7:0) e indicador de estereo (bit 8) direto do chip
  st.rssi   = constrain((int)map(statusRssi & 0x00FF, 0, 70, 0, 5), 0, 5);
  st.stereo = (statusRssi & 0x0100) != 0;

  if (!(statusRssi & 0x8000)) return;          // RDSR: sem grupo novo pronto

  // indicadores de erro por bloco (0=ok, 1/2=corrigido, 3=irrecuperavel)
  uint8_t blerA = (statusRssi >> 9) & 3;
  uint8_t blerB = (readChan   >> 14) & 3;
  uint8_t blerC = (readChan   >> 12) & 3;
  uint8_t blerD = (readChan   >> 10) & 3;

  // Para o texto precisamos de B, C e D fiaveis; o bloco A nao importa.
  if (blerB == 3 || blerC == 3 || blerD == 3) return;

  // Se o bloco A vier irrecuperavel, passamos 0xFFFF (nao-zero) para o parser
  // nao o confundir com o sinal de reset (block1 == 0).
  uint16_t blockA = (blerA == 3) ? 0xFFFF : reg[2];
  rds.processData(blockA, reg[3], reg[4], reg[5]);   // RDSA, RDSB, RDSC, RDSD
}
#endif

static void applyVolume() {
#if USE_SI4703
  radio.setVolume(map(st.volume, 0, 30, 0, 15));
  radio.setMute(st.muted);
#endif
}

static void applyFreq() {
#if USE_SI4703
  radio.setFrequency((uint16_t)lroundf(st.freq * 100));   // unidades de 10 kHz
  st.station = "";
  st.radiotext = "";
  st.hasRDS = false;
  rds.init();
#else
  st.hasRDS = false;
  st.station = "";
  int p = currentPreset();
  if (p >= 0) { st.station = presets[p].name; st.hasRDS = true; }
#endif
}

#if USE_BATTERY
// Estima a percentagem a partir da tensao da celula (curva de descarga 1S
// aproximada por troços). A LiPo nao e linear, por isso usa-se uma tabela.
static int battPercentFromMv(int mv) {
  static const int lut[][2] = {
    {4200, 100}, {4100, 90}, {4000, 80}, {3900, 70}, {3850, 60}, {3800, 55},
    {3750, 45},  {3700, 35}, {3650, 25}, {3600, 15}, {3500,  8}, {3400, 3}, {3300, 0}
  };
  const int n = sizeof(lut) / sizeof(lut[0]);
  if (mv >= lut[0][0])     return 100;
  if (mv <= lut[n - 1][0]) return 0;
  for (int i = 1; i < n; i++)
    if (mv >= lut[i][0]) {
      int v0 = lut[i][0], p0 = lut[i][1], v1 = lut[i - 1][0], p1 = lut[i - 1][1];
      return p0 + (mv - v0) * (p1 - p0) / (v1 - v0);
    }
  return 0;
}

// Le o ADC (media de 8 amostras) e atualiza estado. A primeira leitura e
// imediata; depois e' refrescada a cada 5 s (a tensao varia devagar).
static void pollBattery() {
  static uint32_t last = 0;
  if (st.battValid && millis() - last < 5000) return;
  last = millis();
  uint32_t acc = 0;
  for (int i = 0; i < 8; i++) acc += analogReadMilliVolts(PIN_VBAT);
  int vbatMv = (int)((acc / 8) * VBAT_DIV);   // tensao real da celula (mV)
  st.battV     = vbatMv / 1000.0f;
  st.battPct   = battPercentFromMv(vbatMv);
  st.battValid = true;
}
#endif

// =================== HELPERS DE DESENHO ====================
// --- gestao da fonte smooth ativa ---
// Uma fonte smooth carregada SOBREPOE-SE as fontes numeradas (mesmo passando o
// numero da fonte ao drawString/textWidth). Por isso descarregamo-la sempre
// antes de desenhar/medir texto com fontes numeradas.
static const uint8_t* gFont = nullptr;
static void fontNumbered() {
  if (gFont) { cv.unloadFont(); gFont = nullptr; }
}
static void fontSmooth(const uint8_t* f) {
  if (gFont != f) {
    if (gFont) cv.unloadFont();
    cv.loadFont(f);
    gFont = f;
  }
}

static void txt(const char* s, int x, int y, uint16_t c, uint8_t font,
                uint8_t datum = TL_DATUM) {
  fontNumbered();
  cv.setTextDatum(datum);
  cv.setTextColor(c, COL_BG);
  cv.drawString(s, x, y, font);
}

// texto com fonte smooth (anti-aliased). O AA mistura com o fundo COL_BG, que e
// limpo a cada frame (fillSprite), por isso nao deixa rasto.
static void stxt(const char* s, int x, int y, uint16_t c, const uint8_t* f,
                 uint8_t datum = TL_DATUM) {
  fontSmooth(f);
  cv.setTextDatum(datum);
  cv.setTextColor(c, COL_BG);
  cv.drawString(s, x, y);
}

static void drawSignal(int x, int y, int level) {
  for (int i = 0; i < 5; i++) {
    int h = 2 + i * 2;
    cv.fillRect(x + i * 3, y + (8 - h), 2, h, (i < level) ? COL_GREEN : COL_DKGREY);
  }
}

// icone de bateria; xr = aresta direita, desenha para a esquerda. Devolve a
// largura ocupada (corpo + terminal) para encadear outros elementos.
static int drawBattery(int xr, int y, int pct) {
  const int w = 16, h = 8;
  int x = xr - w;
  cv.fillRect(xr, y + 2, 2, h - 4, COL_GREY);     // terminal (polo +)
  cv.drawRoundRect(x, y, w, h, 1, COL_GREY);       // contorno
  int fw = (w - 2) * constrain(pct, 0, 100) / 100;
  uint16_t c = (pct <= 20) ? COL_ORANGE : COL_GREEN;
  if (fw > 0) cv.fillRect(x + 1, y + 1, fw, h - 2, c);
  return w + 2;
}

static void drawFooter(const char* a, const char* b, const char* c, const char* d) {
  fontNumbered();                      // medicoes/textos do rodape sao fonte 1
  const char* lbl[4] = {a, b, c, d};
  const uint16_t BTN = 0x05B6;         // teal: cor do "botao" (chip) das acoes
  cv.setTextFont(1);
  cv.setTextDatum(TL_DATUM);
  int cw = LW / 4;
  for (int i = 0; i < 4; i++) {
    int x0 = i * cw;
    int bx = x0 + 3, bw = cw - 6, by = 62, bh = 12;
    cv.fillSmoothRoundRect(bx, by, bw, bh, 3, BTN, COL_BG);  // chip como o preset

    String s = lbl[i];
    char num[2] = {s[0], 0};
    String rest = s.substring(1);                  // inclui o espaco inicial
    int nw = cv.textWidth(num), rw = cv.textWidth(rest);
    int sx = bx + (bw - (nw + rw)) / 2;
    cv.setTextColor(0xFFE0, BTN);                  // numero a amarelo (bom contraste no teal)
    cv.drawString(num, sx, by + 3);
    cv.setTextColor(COL_WHITE, BTN);               // etiqueta a branco
    cv.drawString(rest.c_str(), sx + nw, by + 3);
  }
}

static void drawFrame() {
  cv.fillSprite(COL_BG);
  cv.drawRoundRect(0, 0, LW, LH, 4, COL_FRAME);
}

static void drawScale(int y, float fMin, float fMax, float fCur, bool ticks) {
  int x0 = 14, x1 = LW - 14;
  cv.drawFastHLine(x0, y, x1 - x0, COL_DKGREY);
  if (ticks)
    for (float f = fMin; f <= fMax + 0.1; f += 2) {
      int x = x0 + (int)((f - fMin) / (fMax - fMin) * (x1 - x0));
      cv.drawFastVLine(x, y - 2, 4, COL_GREY);
    }
  int xp = x0 + (int)((fCur - fMin) / (fMax - fMin) * (x1 - x0));
  cv.fillSmoothCircle(xp, y, 3, COL_CYAN, COL_BG);
}

static void drawBigFreq(const char* num, uint16_t col, int yc) {
  fontSmooth(FF_BIG);
  int fw = cv.textWidth(num);
  fontNumbered();
  int mw = cv.textWidth("MHz", 2);
  int x = (LW - (fw + 6 + mw)) / 2;
  stxt(num, x, yc, col, FF_BIG, ML_DATUM);
  txt("MHz", x + fw + 6, yc + 1, COL_GREY, 2, ML_DATUM);
}

static void scrollText(const char* s, int x, int y, int w, int h, uint16_t col) {
  const uint8_t FNT = 2;               // fonte 2 (16px) - um pouco menor que FreeSans
  fontNumbered();                      // garante fonte numerada (nao a smooth)
  cv.setViewport(x, y, w, h, true);
  cv.setTextColor(col, COL_BG);
  int tw = cv.textWidth(s, FNT);
  int yc = h / 2;
  if (tw <= w) {
    cv.setTextDatum(MC_DATUM);
    cv.drawString(s, w / 2, yc, FNT);
  } else {
    cv.setTextDatum(ML_DATUM);
    int gap = 40, period = tw + gap;
    int off = (millis() / 55) % period;
    cv.drawString(s, -off, yc, FNT);
    cv.drawString(s, -off + period, yc, FNT);
  }
  cv.setTextFont(1);
  cv.resetViewport();
}

static void triLeft(int x, int y, int s, uint16_t c)  { cv.fillTriangle(x, y, x + s, y - s, x + s, y + s, c); }
static void triRight(int x, int y, int s, uint16_t c) { cv.fillTriangle(x, y, x - s, y - s, x - s, y + s, c); }

// ================= ICONES (anti-aliased) ===================
// estilo Lucide/Feather: traco ~2px, cantos arredondados, aneis anti-aliased
static void icRadio(int cx, int cy, uint16_t c) {
  // antena diagonal com ponta
  cv.drawWedgeLine(cx + 1, cy - 5, cx + 8, cy - 12, 1.4, 1.4, c, COL_BG);
  cv.fillSmoothCircle(cx + 8, cy - 12, 1, c, COL_BG);
  // corpo (caixa arredondada)
  cv.drawSmoothRoundRect(cx - 11, cy - 5, 3, 2, 22, 13, c, COL_BG);
  // botao/dial
  cv.fillSmoothCircle(cx + 5, cy + 2, 3, c, COL_BG);
  cv.fillSmoothCircle(cx + 5, cy + 2, 1, COL_BG, c);
  // grelha (2 barras arredondadas)
  cv.fillSmoothRoundRect(cx - 8, cy - 1, 8, 2, 1, c, COL_BG);
  cv.fillSmoothRoundRect(cx - 8, cy + 3, 6, 2, 1, c, COL_BG);
}
static void icStar(int cx, int cy, uint16_t c) {
  // marcador (bookmark) preenchido com entalhe em V em baixo
  int w = 13, h = 19, x = cx - w / 2, y = cy - h / 2;
  cv.fillSmoothRoundRect(x, y, w, h, 2, c, COL_BG);
  cv.fillTriangle(x - 1, y + h, x + w + 1, y + h, cx, y + h - 7, COL_BG);
}
static void icSpeaker(int cx, int cy, uint16_t c) {
  cv.fillRect(cx - 9, cy - 3, 5, 6, c);
  cv.fillTriangle(cx - 4, cy, cx + 2, cy - 7, cx + 2, cy + 7, c);
  cv.drawSmoothArc(cx + 2, cy, 8, 7, 235, 305, c, COL_BG, true);
  cv.drawSmoothArc(cx + 2, cy, 12, 11, 235, 305, c, COL_BG, true);
}
static void icSearch(int cx, int cy, uint16_t c) {
  cv.drawSmoothCircle(cx - 2, cy - 2, 7, c, COL_BG);
  cv.drawSmoothCircle(cx - 2, cy - 2, 6, c, COL_BG);
  cv.drawWideLine(cx + 3, cy + 3, cx + 9, cy + 9, 2.5, c, COL_BG);
}
static void icInfo(int cx, int cy, uint16_t c) {
  cv.drawSmoothCircle(cx, cy, 10, c, COL_BG);
  cv.fillSmoothCircle(cx, cy - 4, 1.6, c, COL_BG);
  cv.fillRect(cx - 1, cy - 1, 2, 7, c);
}
// pequeno icone "broadcast": ponto central + ondas dos dois lados
static void icBroadcast(int cx, int cy, uint16_t c) {
  cv.fillSmoothCircle(cx, cy, 2, c, COL_BG);
  cv.drawSmoothArc(cx, cy, 5, 4, 235, 305, c, COL_BG, true);   // onda direita
  cv.drawSmoothArc(cx, cy, 5, 4,  55, 125, c, COL_BG, true);   // onda esquerda
}

// ===================== ECRAS ===============================
static void screenSplash(int f) {
  drawFrame();
  int ax = 38, ay = 30;
  cv.drawWideLine(ax, ay + 20, ax, ay - 4, 2, COL_GREY, COL_BG);
  cv.fillTriangle(ax - 7, ay + 22, ax + 7, ay + 22, ax, ay + 12, COL_GREY);
  cv.fillSmoothCircle(ax, ay - 6, 3, COL_ORANGE, COL_BG);
  for (int i = 0; i < 3; i++) {
    int r = 7 + ((f * 2 + i * 9) % 27);
    cv.drawSmoothArc(ax, ay - 6, r, r - 1, 235, 305, (r < 20) ? COL_CYAN : COL_FRAME, COL_BG, true);
  }
  stxt("FM RADIO", 178, 22, COL_CYAN, FF_BIG, MC_DATUM);
  cv.fillRect(120, 38, 116, 2, COL_ORANGE);
  txt("Sintonizador Estereo", 178, 43, COL_GREY, 1, TC_DATUM);
  int bx = 74, by = 67, bw = 4, gap = 2;
  for (int i = 0; i < 30; i++) {
    int h = 2 + (int)(9 * (0.5 + 0.5 * sin(f * 0.35 + i * 0.55)));
    cv.fillRect(bx + i * (bw + gap), by - h, bw, h, (h > 8) ? COL_ORANGE : COL_GREEN);
  }
}

// Design "cartao de estacao"
static void screenMain() {
  drawFrame();

  // ---- barra de estado (topo) ----
  icBroadcast(11, 7, st.stereo ? COL_GREEN : COL_GREY);
  txt(st.stereo ? "STEREO" : "MONO", 20, 4, st.stereo ? COL_GREEN : COL_GREY, 1);

  // volume ao centro do topo (comum aos dois modos)
  String vol = st.muted ? String("MUTE") : ("VOL " + String(st.volume));
  txt(vol.c_str(), LW / 2, 4, st.muted ? COL_ORANGE : COL_CYAN, 1, TC_DATUM);

  int rx = LW - 10;
#if USE_BATTERY
  if (st.battValid) {                      // bateria no canto direito
    rx -= drawBattery(rx, 2, st.battPct);
    rx -= 4;
  }
#endif
  int pIdx = currentPreset();
  if (pIdx >= 0) {                         // chip da memoria (canto direito)
    char pn[5]; snprintf(pn, sizeof(pn), "P%02d", pIdx + 1);
    int w = 24, h = 11, x = rx - w, y = 1;
    cv.fillSmoothRoundRect(x, y, w, h, 3, COL_ORANGE, COL_BG);
    cv.setTextFont(1);
    cv.setTextDatum(TC_DATUM);
    cv.setTextColor(COL_BG, COL_ORANGE);   // texto preto com fundo laranja (chip)
    cv.drawString(pn, x + w / 2, y + 2);
    rx = x - 6;
  }
  drawSignal(rx - 15, 4, st.rssi);

  if (st.hasRDS) {
    // ---- cartao: barra de acento + nome + frequencia ----
    cv.fillSmoothRoundRect(8, 16, 3, 26, 1, COL_CYAN, COL_BG);
    stxt(st.station.c_str(), 16, 29, COL_WHITE, FF_BIG, ML_DATUM);
    txt((f2(st.freq) + " MHz").c_str(), LW - 12, 29, COL_CYAN, 2, MR_DATUM);
    // separador + ticker do radiotext (altura >= 16 para nao cortar a fonte)
    cv.drawFastHLine(8, 44, LW - 16, COL_FRAME);
    scrollText(st.radiotext.c_str(), 12, 45, LW - 24, 16, 0xC618);  // prata claro (mais visivel que grey)
  } else {
    // ---- sem RDS: frequencia grande + dial da banda ----
    drawBigFreq(f2(st.freq).c_str(), COL_WHITE, 30);
    drawScale(52, FM_MIN, FM_MAX, st.freq, true);
  }

  drawFooter("1 -", "2 +", "3 VOL", "4 PRESET");
}

static void screenVolume() {
  drawFrame();
  icSpeaker(20, 11, st.muted ? COL_GREY : COL_CYAN);
  stxt("VOLUME", LW / 2, 3, COL_CYAN, FF_TITLE, TC_DATUM);
  txt(st.stereo ? "STEREO" : "MONO", LW - 10, 4, st.stereo ? COL_GREEN : COL_GREY, 1, TR_DATUM);
  if (st.muted) txt("MUTE", 10, 4, COL_ORANGE, 1);

  char vb[4]; snprintf(vb, sizeof(vb), "%d", st.volume);
  stxt(st.muted ? "--" : vb, LW / 2, 33, st.muted ? COL_GREY : COL_WHITE, FF_BIG, MC_DATUM);

  int x0 = 24, x1 = LW - 24, y = 50, n = 30;
  int seg = (x1 - x0) / n;
  for (int i = 0; i < n; i++) {
    uint16_t c = (!st.muted && i < st.volume) ? COL_CYAN : COL_DKGREY;
    cv.fillRect(x0 + i * seg, y, seg - 1, 6, c);
  }
  txt("0", 14, y - 1, COL_GREY, 1);
  txt("30", LW - 14, y - 1, COL_GREY, 1, TR_DATUM);
  drawFooter("1 -", "2 +", "3 MUTE", "4 OK");
}

static void presetBox(int x, int idx, bool sel) {
  int w = 56, h = 36, y = 20;
  char pn[5]; snprintf(pn, sizeof(pn), "P%02d", idx + 1);
  cv.drawRoundRect(x, y, w, h, 3, sel ? COL_CYAN : COL_FRAME);
  txt(pn, x + w / 2, y + 2, sel ? COL_CYAN : COL_ORANGE, 2, TC_DATUM);
  txt(f2(presets[idx].f).c_str(), x + w / 2, y + 18, COL_WHITE, 1, TC_DATUM);
  txt(presets[idx].name, x + w / 2, y + 27, COL_GREY, 1, TC_DATUM);
}
static void screenPresets() {
  drawFrame();
  stxt("PRESETS", LW / 2, 3, COL_CYAN, FF_TITLE, TC_DATUM);

  if (nPresets == 0) {
    txt("Sem memorias guardadas", LW / 2, 30, COL_GREY, 2, MC_DATUM);
    txt("Usa o SCAN para procurar estacoes", LW / 2, 46, COL_DKGREY, 1, TC_DATUM);
    drawFooter("1 <", "2 >", "3 OK", "4 SAIR");
    return;
  }

  int pages = (nPresets + 3) / 4;
  int page = st.presetSel / 4;
  char pg[12]; snprintf(pg, sizeof(pg), "PAG %d/%d", page + 1, pages);
  txt(pg, LW - 8, 4, COL_GREY, 1, TR_DATUM);
  txt("seg.3 apaga", 8, 4, COL_DKGREY, 1);   // dica: clique longo no 3 apaga
  triLeft(8, 38, 5, COL_GREY);
  triRight(LW - 8, 38, 5, COL_GREY);
  int x = 18, step = 62;
  for (int i = 0; i < 4; i++) {
    int idx = page * 4 + i;
    if (idx >= nPresets) break;
    presetBox(x + i * step, idx, idx == st.presetSel);
  }
  drawFooter("1 <", "2 >", "3 OK", "4 SAIR");
}

String gScanInfo = "";   // texto de estado do scan

static void screenScan() {
  drawFrame();
  txt("FM", 8, 4, COL_CYAN, 1);
  txt(st.stereo ? "STEREO" : "MONO", 30, 4, st.stereo ? COL_GREEN : COL_GREY, 1);
  drawSignal(80, 4, st.rssi);
  char mem[12]; snprintf(mem, sizeof(mem), "MEM %d/%d", nPresets, MAX_PRESET);
  txt(mem, LW - 10, 4, COL_ORANGE, 1, TR_DATUM);
  drawBigFreq(f2(st.freq).c_str(), COL_WHITE, 31);
  drawScale(48, FM_MIN, FM_MAX, st.freq, false);
  txt(gScanInfo.c_str(), LW / 2, 53,
      st.scanning ? COL_CYAN : COL_GREEN, 1, TC_DATUM);
  drawFooter(st.scanning ? "1 STOP" : "1 SCAN", "2 -", "3 -", "4 SAIR");
}

static void menuItem(int cx, const char* lbl, void (*ic)(int, int, uint16_t), bool sel) {
  if (sel) cv.drawSmoothRoundRect(cx - 25, 7, 4, 3, 50, 45, COL_CYAN, COL_BG);
  ic(cx, 23, sel ? COL_CYAN : COL_WHITE);
  txt(lbl, cx, 42, sel ? COL_CYAN : COL_GREY, 1, TC_DATUM);
}
static void screenMenu() {
  drawFrame();
  void (*ics[5])(int, int, uint16_t) = {icRadio, icStar, icSpeaker, icSearch, icInfo};
  const char* lbls[5] = {"RADIO", "PRESETS", "VOLUME", "SCAN", "SOBRE"};
  int x0 = 28, step = (LW - 56) / 4;
  for (int i = 0; i < 5; i++)
    menuItem(x0 + i * step, lbls[i], ics[i], i == st.menuSel);
  drawFooter("1 <", "2 >", "3 OK", "4 SAIR");
}

static void screenMessage() {
  drawFrame();
  icInfo(34, 26, COL_CYAN);
  txt(st.msgTitle.c_str(), 60, 10, COL_CYAN, 1);
  txt(st.msgL1.c_str(), 60, 22, COL_WHITE, 2);
  txt(st.msgL2.c_str(), 60, 42, COL_GREY, 1);
  if (st.msgCheck) {
    int kx = LW - 34, ky = 28;
    cv.drawSmoothCircle(kx, ky, 12, COL_GREEN, COL_BG);
    cv.drawWideLine(kx - 6, ky, kx - 2, ky + 5, 2, COL_GREEN, COL_BG);
    cv.drawWideLine(kx - 2, ky + 5, kx + 6, ky - 5, 2, COL_GREEN, COL_BG);
  }
  drawFooter("1 <", "2 >", "3 OK", "4 SAIR");
}

// screenTune precisa do cabecalho comum -> redefinido aqui de forma completa
static void drawTune() {
  drawFrame();
  txt("FM", 8, 4, COL_CYAN, 1);
  txt(st.stereo ? "STEREO" : "MONO", 30, 4, st.stereo ? COL_GREEN : COL_GREY, 1);
  drawSignal(80, 4, st.rssi);
  txt("TUNE", LW - 10, 4, COL_ORANGE, 1, TR_DATUM);
  // topo-centro: nome RDS (quando disponivel) ou o passo atual
  if (st.hasRDS && st.station.length()) {
    txt(st.station.c_str(), LW / 2, 4, COL_GREEN, 1, TC_DATUM);
  } else {
    char ps[24]; snprintf(ps, sizeof(ps), "passo %.2f (seg.2)", st.step);
    txt(ps, LW / 2, 4, COL_DKGREY, 1, TC_DATUM);
  }
  triLeft(16, 31, 6, COL_CYAN);
  triRight(LW - 16, 31, 6, COL_CYAN);
  drawBigFreq(f2(st.freq).c_str(), COL_WHITE, 31);
  txt("88.0", 14, 48, COL_GREY, 1);
  txt("108.0", LW - 14, 48, COL_GREY, 1, TR_DATUM);
  drawScale(56, 88, 108, st.freq, true);
  drawFooter("1 -", "2 +", "3 GUARDAR", "4 SAIR");
}

// ---------- render para o painel (offset + rotacao 90) -----
static void show() {
  tft.startWrite();
  tft.setAddrWindow(OX, OY, PW, PH);
  for (int16_t ny = 0; ny < PH; ny++) {
    int16_t lx = PH - 1 - ny;
    for (int16_t nx = 0; nx < PW; nx++)
      tft.pushColor(cv.readPixel(lx, nx));
  }
  tft.endWrite();
}

static void render() {
  switch (st.screen) {
    case SC_MAIN:    screenMain();    break;
    case SC_TUNE:    drawTune();      break;
    case SC_VOLUME:  screenVolume();  break;
    case SC_PRESETS: screenPresets(); break;
    case SC_SCAN:    screenScan();    break;
    case SC_MENU:    screenMenu();    break;
    case SC_MSG:     screenMessage(); break;
  }
}

// ===================== NAVEGACAO ===========================
static void showMessage(const String& t, const String& l1, const String& l2,
                        bool check, Screen ret) {
  st.msgTitle = t; st.msgL1 = l1; st.msgL2 = l2; st.msgCheck = check;
  st.msgReturn = ret; st.msgUntil = millis() + 2500; st.screen = SC_MSG;
}

static void clampFreq() { if (st.freq < 87.5f) st.freq = 87.5f; if (st.freq > 108.0f) st.freq = 108.0f; }

static void savePreset() {
  // adiciona a frequencia atual como nova memoria
  if (nPresets >= MAX_PRESET) {
    showMessage("MEMORIA CHEIA", "Maximo de 20", "memorias atingido", false, st.screen);
    return;
  }
  storePreset(st.freq, st.hasRDS ? st.station : String(""));
  savePresetsNVS();
  char l1[16]; snprintf(l1, sizeof(l1), "%.2f MHz", st.freq);
  char l2[16]; snprintf(l2, sizeof(l2), "na MEMORIA %02d", nPresets);
  showMessage("ESTACAO GUARDADA", l1, l2, true, st.screen);
}

// ---- SCAN automatico com auto-store ----
enum ScanPhase { SP_SEEK, SP_DWELL };
static ScanPhase scanPhase;
static uint32_t  scanDwellStart;

static void startScan() {
  nPresets = 0;                       // apaga todas as memorias
  st.freq = FM_MIN;                   // comeca no inicio da banda
#if USE_SI4703
  radio.setFrequency((uint16_t)lroundf(FM_MIN * 100));
  rds.init();
#endif
  st.station = ""; st.radiotext = ""; st.hasRDS = false;
  st.scanning = true;
  scanPhase = SP_SEEK;
  gScanInfo = "A procurar estacao...";
  st.screen = SC_SCAN;
}

static void finishScan() {
  st.scanning = false;
  savePresetsNVS();
  markSettingsDirty();
  char b[24]; snprintf(b, sizeof(b), "Concluido: %d memorias", nPresets);
  gScanInfo = b;
}

// abre os presets com a memoria da frequencia atual ja selecionada
static void enterPresets() {
  int p = currentPreset();
  if (p >= 0) st.presetSel = p;
  st.screen = SC_PRESETS;
}

// abre o ecra de scan SEM comecar: o utilizador inicia com a tecla 1
static void enterScan() {
  st.scanning = false;
  gScanInfo = "Prima 1 para procurar";
  st.screen = SC_SCAN;
}

// ev: 1..4 clique curto ; 11..14 clique longo
static void handleEvent(int ev) {
  if (st.screen == SC_MSG) { st.screen = st.msgReturn; return; }

  // --- atalhos globais (clique longo) ---
  if (ev == 14) { st.screen = SC_MENU; return; }                 // longo 4 -> MENU
  if (ev == 11) { st.screen = SC_TUNE; return; }                 // longo 1 -> sintonia

  switch (st.screen) {
    case SC_MAIN:
      if (ev == 1) { st.freq -= st.step; clampFreq(); applyFreq(); markSettingsDirty(); }
      if (ev == 2) { st.freq += st.step; clampFreq(); applyFreq(); markSettingsDirty(); }
      if (ev == 3) st.screen = SC_VOLUME;
      if (ev == 4) enterPresets();
      break;
    case SC_VOLUME:
      if (ev == 1 && st.volume > 0)  st.volume--;
      if (ev == 2 && st.volume < 30) st.volume++;
      if (ev == 3) st.muted = !st.muted;
      if (ev == 4) st.screen = SC_MAIN;
      if (ev >= 1 && ev <= 3) { applyVolume(); markSettingsDirty(); }
      break;
    case SC_TUNE:
      if (ev == 1) { st.freq -= st.step; clampFreq(); applyFreq(); markSettingsDirty(); }
      if (ev == 2) { st.freq += st.step; clampFreq(); applyFreq(); markSettingsDirty(); }
      if (ev == 12) st.step = (st.step == 0.10f) ? 0.05f : 0.10f;  // PASSO: clique longo 2
      if (ev == 3) savePreset();
      if (ev == 4) st.screen = SC_MAIN;
      break;
    case SC_PRESETS:
      if (nPresets == 0) { if (ev == 4) st.screen = SC_MAIN; break; }
      if (ev == 1 && st.presetSel > 0)            st.presetSel--;
      if (ev == 2 && st.presetSel < nPresets - 1) st.presetSel++;
      if (ev == 3) { st.freq = presets[st.presetSel].f; applyFreq(); markSettingsDirty(); st.screen = SC_MAIN; }
      if (ev == 13) {                                  // clique longo 3 -> apaga memoria
        char l1[16]; snprintf(l1, sizeof(l1), "%.2f MHz", presets[st.presetSel].f);
        removePreset(st.presetSel);
        savePresetsNVS();
        showMessage("MEMORIA APAGADA", l1, "", false, nPresets ? SC_PRESETS : SC_MAIN);
      }
      if (ev == 4) st.screen = SC_MAIN;
      break;
    case SC_SCAN:
      if (!st.scanning) {
        if (ev == 1) startScan();                       // inicia a procura
      } else {
        if (ev == 1) finishScan();                      // STOP (mantem memorias)
      }
      if (ev == 4) { st.scanning = false; st.screen = SC_MAIN; }
      break;
    case SC_MENU:
      if (ev == 1 && st.menuSel > 0) st.menuSel--;
      if (ev == 2 && st.menuSel < 4) st.menuSel++;
      if (ev == 3) {
        switch (st.menuSel) {
          case 0: st.screen = SC_MAIN; break;
          case 1: enterPresets(); break;
          case 2: st.screen = SC_VOLUME; break;
          case 3: enterScan(); break;
          case 4: showMessage("SOBRE", "FM Radio v0.1", "ESP32-S2 + SI4703", false, SC_MENU); break;
        }
      }
      if (ev == 4) st.screen = SC_MAIN;
      break;
    default: break;
  }
}

// scan automatico: procura estacoes e guarda as que tenham ESTEREO + nome RDS
static const uint32_t SCAN_DWELL_MS  = 7000;  // tempo maximo de espera por RDS
static const uint32_t SCAN_SETTLE_MS = 1500;  // nome tem de manter-se estavel
static String   gNameCand;                    // nome candidato a estabilizar
static uint32_t gNameSince;                   // desde quando esta estavel

static void updateScan() {
  if (!st.scanning) return;

#if USE_SI4703
  if (scanPhase == SP_SEEK) {
    float prev = st.freq;
    radio.seekUp(true);                       // bloqueia ate proxima estacao
    float nf = radio.getFrequency() / 100.0f;
    // deu a volta a banda (frequencia desceu) ou memorias cheias -> fim
    if (nPresets >= MAX_PRESET || nf <= prev + 0.01f) {
      st.freq = nf;
      finishScan();
      return;
    }
    st.freq = nf;
    st.station = ""; st.radiotext = ""; st.hasRDS = false;
    gNameCand = "";
    rds.init();
    gScanInfo = "A obter RDS...";
    scanDwellStart = millis();
    scanPhase = SP_DWELL;
  } else {  // SP_DWELL
    radio.checkRDS();
    RADIO_INFO info; radio.getRadioInfo(&info);
    st.stereo = info.stereo;
    String name = st.station; name.trim();

    if (info.stereo && name.length()) {
      // espera que o nome se mantenha igual durante SCAN_SETTLE_MS
      if (name != gNameCand) { gNameCand = name; gNameSince = millis(); }
      else if (millis() - gNameSince >= SCAN_SETTLE_MS) {
        storePreset(st.freq, gNameCand);      // guarda nome estabilizado!
        gScanInfo = "Guardada: " + gNameCand;
        scanPhase = SP_SEEK;
      }
    } else if (millis() - scanDwellStart > SCAN_DWELL_MS) {
      gScanInfo = "A procurar estacao...";    // sem estereo/RDS estavel -> ignora
      scanPhase = SP_SEEK;
    }
  }
#else
  // simulacao: gera algumas memorias
  static uint32_t t = 0;
  if (millis() - t < 200) return;
  t = millis();
  st.freq += 0.5f;
  if (st.freq >= FM_MAX || nPresets >= MAX_PRESET) { st.freq = FM_MAX; finishScan(); return; }
  if (((int)(st.freq * 10) % 15) == 0) storePreset(st.freq, "ESTACAO " + String(nPresets + 1));
#endif
}

// ===================== ENTRADA (botoes + serial) ===========
static int pollButtons() {
  static bool down[4] = {false, false, false, false};
  static uint32_t tDown[4];
  static bool longSent[4] = {false, false, false, false};
  const uint32_t LONG_MS = 700, DEBOUNCE = 25;
  for (int i = 0; i < 4; i++) {
    bool pressed = (digitalRead(BTN_PIN[i]) == LOW);
    if (pressed && !down[i]) { down[i] = true; tDown[i] = millis(); longSent[i] = false; }
    else if (pressed && down[i]) {
      if (!longSent[i] && millis() - tDown[i] >= LONG_MS) { longSent[i] = true; return 11 + i; }
    } else if (!pressed && down[i]) {
      down[i] = false;
      if (!longSent[i] && millis() - tDown[i] >= DEBOUNCE) return 1 + i;
    }
  }
  return 0;
}

static int pollSerial() {
  if (!Serial.available()) return 0;
  char c = Serial.read();
  if (c >= '1' && c <= '4') return c - '0';
  if (c == 'q') return 11;
  if (c == 'w') return 12;
  if (c == 'e') return 13;
  if (c == 'r') return 14;
  return 0;
}

void setup() {
  Serial.begin(115200);
  // COM do teclado vai a GND (nao a um GPIO), por isso so configuramos as teclas.
  for (int i = 0; i < 4; i++) pinMode(BTN_PIN[i], INPUT_PULLUP);

#if USE_BATTERY
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_VBAT, ADC_11db);   // fundo de escala ~3.1V no pino
#endif

  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(false);
  tft.fillScreen(TFT_BLACK);
  cv.setColorDepth(16);
  cv.createSprite(LW, LH);

  // splash primeiro: garante que o LCD desenha mesmo que o radio falhe
  for (int i = 0; i < 70; i++) { screenSplash(i); show(); delay(50); }

  // restaura ultima sintonia/volume e memorias
  loadSettingsNVS();
  loadPresetsNVS();

#if USE_SI4703
  Serial.println("SI4703: a inicializar...");
  // Reset manual em modo 2-wire (SDIO a LOW durante o flanco de subida do RST)
  pinMode(PIN_RST, OUTPUT);
  pinMode(PIN_SDA, OUTPUT);
  digitalWrite(PIN_SDA, LOW);
  digitalWrite(PIN_RST, LOW);
  delay(10);
  digitalWrite(PIN_RST, HIGH);
  delay(10);
  // Agora liga o I2C (nao damos RESETPIN a lib para nao repetir o reset)
  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(100000);
  delay(10);
  radio.initWire(Wire);
  bool ok = radio.init();
  Serial.printf("SI4703: init %s\n", ok ? "OK" : "FALHOU (verificar ligacoes)");
  radio.setBandFrequency(RADIO_BAND_FM, (uint16_t)lroundf(st.freq * 100));
  radio.setMono(false);
  radio.setMute(false);
  radio.setVolume(map(st.volume, 0, 30, 0, 15));
  radio.attachReceiveRDS(rdsProcess);
  rds.attachServiceNameCallback(rdsServiceName);
  rds.attachTextCallback(rdsTextCb);
#endif

  applyFreq();

  Serial.println("\n== FM Radio ==  teclas: 1-4 (curto)  q w e r (longo)");
}

void loop() {
  static uint32_t lastActivity = 0;

  int ev = pollSerial();
  if (!ev) ev = pollButtons();
  if (ev) {
    // log para ajudar a validar os 4 botoes no hardware (passo dos proximos passos)
    if (ev >= 1 && ev <= 4)        Serial.printf("BTN %d: clique curto\n", ev);
    else if (ev >= 11 && ev <= 14) Serial.printf("BTN %d: clique longo\n", ev - 10);
    handleEvent(ev); lastActivity = millis();
  }

  updateScan();

#if USE_BATTERY
  pollBattery();
#endif

#if USE_SI4703
  // RDS + RSSI/estereo lidos diretamente do chip (ver pollSI4703RDS).
  if (!st.scanning) pollSI4703RDS();
#endif

  if (st.screen == SC_MSG && millis() > st.msgUntil) st.screen = st.msgReturn;

  // grava definicoes 2s depois da ultima alteracao (sintonia/volume)
  if (settingsDirty && millis() - settingsDirtyAt > 2000) {
    settingsDirty = false;
    saveSettingsNVS();
  }

  // inatividade: ao fim de 30s volta ao principal (excepto durante o scan)
  if (st.screen != SC_MAIN && st.screen != SC_MSG && !st.scanning &&
      millis() - lastActivity > 30000) {
    st.screen = SC_MAIN;
  }

  render();
  show();
  delay(15);
}
