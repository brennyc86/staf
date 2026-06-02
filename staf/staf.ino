/*
 * STAF — cosplay lichtstaf
 * --------------------------------------------------------------------------
 * ATtiny85 op 2x AA, 1 WS2812 (NeoPixel) + 3 drukknoppen.
 *
 * Hardware (fysieke pin -> Arduino pin in ATTinyCore):
 *   - Wit knopje    : fysiek pin 2  = D3
 *   - Blauw knopje  : fysiek pin 3  = D4
 *   - Rood knopje   : fysiek pin 7  = D2
 *   - NeoPixel data : fysiek pin 5  = D0
 *
 * Knoppen zijn naar GND geschakeld. De interne pull-up (INPUT_PULLUP) staat
 * aan; een externe 10k pull-up mag er gerust parallel bij. Ingedrukt = LOW.
 *
 * KLEURVOLGORDE: deze WS2812 bleek RGB i.p.v. GRB (rood en groen wisselden
 * om). Staat hieronder als PIXEL_VOLGORDE. Klopt een kleur niet? Wissel
 * tussen NEO_RGB en NEO_GRB.
 *
 * BELANGRIJK: NeoPixels hebben strakke timing nodig. Zet de ATtiny85 op
 * 8 MHz (intern) of 16 MHz (PLL) via Tools -> Clock en daarna eenmalig
 * "Bootloader branden". Op 1 MHz werkt de NeoPixel NIET betrouwbaar.
 *
 * ── BEDIENING ─────────────────────────────────────────────────────────────
 * KORT drukken kiest een kleur (lamp gaat aan en "ademt" zacht en
 * onregelmatig):
 *   - rood  1x          -> ROOD
 *   - blauw 1x          -> GROEN
 *   - blauw 2x (<1,5 s) -> BLAUW
 *   - wit   1x          -> WIT
 * Twee knoppen samen (overlappend, starts binnen 1 s) mengt:
 *   - rood + blauw      -> PAARS
 *   - blauw + wit       -> GEEL
 *   - rood + wit        -> ORANJE
 * Nogmaals dezelfde kleur/combo kort drukken -> UIT (toggle).
 * LANG vasthouden = tijdelijke felheid in de HUIDIGE kleur:
 *   - rood vast         -> heel fel faden zolang ingedrukt
 *   - blauw vast        -> wat minder fel faden zolang ingedrukt
 *   - loslaten          -> terug naar zacht faden
 *   - wit vast          -> 1 korte felle flits en daarna uit
 */

#include <Adafruit_NeoPixel.h>

// ---- Pinnen ----------------------------------------------------------------
#define PIN_ROOD    2   // fysiek pin 7  -> knop-index ROOD
#define PIN_BLAUW   4   // fysiek pin 3  -> knop-index BLAUW
#define PIN_WIT     3   // fysiek pin 2  -> knop-index WIT
#define PIN_PIXEL   0   // fysiek pin 5

#define PIXEL_VOLGORDE (NEO_RGB + NEO_KHZ800)   // RGB-strip; zet NEO_GRB indien omgewisseld

// ---- Knop-indexen ----------------------------------------------------------
#define ROOD   0
#define BLAUW  1
#define WIT    2

// ---- Tijden ----------------------------------------------------------------
#define DEBOUNCE_MS      25      // ontdender
#define LANG_DREMPEL_MS  600     // >= dit ingedrukt = "lang vasthouden"
#define DUBBEL_WINDOW_MS 1500    // 2e tik op blauw moet binnen dit venster
#define COMBO_WINDOW_MS  1000    // twee knoppen samen: starts binnen dit venster
#define WIT_FLITS_MS     140     // duur van de witte-knop flits
#define ADEM_STAP_MS     12      // tijd per helderheidsstapje (vloeiendheid)

// ---- Felheidsbereiken {min,max} voor de adembeweging -----------------------
#define ZACHT_MIN   4
#define ZACHT_MAX   40
#define MIDDEN_MIN  70
#define MIDDEN_MAX  160
#define HOOG_MIN    170
#define HOOG_MAX    255

enum Felheid { F_ZACHT, F_MIDDEN, F_HOOG };

Adafruit_NeoPixel pixel(1, PIN_PIXEL, PIXEL_VOLGORDE);

// ---- Knopstatus ------------------------------------------------------------
struct Knop {
  uint8_t  pin;
  bool     stabiel;        // true = ingedrukt (ontdenderd)
  bool     vorige;         // laatste ruwe meting
  uint32_t verMs;          // moment ruwe meting veranderde
  uint32_t neerMs;         // moment van indrukken
  uint32_t duur;           // duur van de zojuist afgesloten druk
  bool     justDown;       // deze ronde net ingedrukt
  bool     justUp;         // deze ronde net losgelaten
  bool     isHold;         // huidige druk is een lang-vasthouden
  bool     consumed;       // huidige druk hoort bij een combo (geen tik)
  uint8_t  taps;           // korte tikken (voor blauw enkel/dubbel)
  uint32_t laatsteTapMs;   // moment van laatste tik
};

Knop knoppen[3] = {
  {PIN_ROOD,  false, false, 0, 0, 0, false, false, false, false, 0, 0},
  {PIN_BLAUW, false, false, 0, 0, 0, false, false, false, false, 0, 0},
  {PIN_WIT,   false, false, 0, 0, 0, false, false, false, false, 0, 0},
};

// Combo-paren en hun mengkleur (r,g,b).
const uint8_t comboPaar[3][2]  = { {ROOD, BLAUW}, {BLAUW, WIT}, {ROOD, WIT} };
const uint8_t comboKleur[3][3] = { {170, 0, 255}, {255, 255, 0}, {255, 90, 0} };

// ---- Actieve lichtstaat ----------------------------------------------------
bool     aan      = false;
uint8_t  actR = 255, actG = 0, actB = 0;   // huidige kleur
uint8_t  lastR = 255, lastG = 0, lastB = 0; // laatst gekozen kleur (voor boost/flits)
Felheid  felheid  = F_ZACHT;
bool     comboBezig = false;

// adembeweging
uint8_t  ademNu     = 0;
uint8_t  ademDoel   = 0;
uint32_t ademVorigMs = 0;

void setup() {
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(knoppen[i].pin, INPUT_PULLUP);
    knoppen[i].vorige  = (digitalRead(knoppen[i].pin) == LOW);
    knoppen[i].stabiel = knoppen[i].vorige;
  }
  pixel.begin();
  pixel.clear();
  pixel.show();
  randomSeed(analogRead(0));
}

void loop() {
  uint32_t nu = millis();
  for (uint8_t i = 0; i < 3; i++) leesKnop(i, nu);
  checkCombo(nu);
  checkHold(nu);
  checkTaps(nu);
  ademLus(nu);
}

// Ontdender + flank-detectie voor één knop.
void leesKnop(uint8_t i, uint32_t nu) {
  Knop &k = knoppen[i];
  k.justDown = false;
  k.justUp   = false;

  bool meting = (digitalRead(k.pin) == LOW);   // LOW = ingedrukt
  if (meting != k.vorige) { k.vorige = meting; k.verMs = nu; }

  if ((nu - k.verMs) >= DEBOUNCE_MS && meting != k.stabiel) {
    k.stabiel = meting;
    if (meting) { k.neerMs = nu; k.justDown = true; }
    else        { k.duur = nu - k.neerMs; k.justUp = true; }
  }
}

uint8_t aantalIn() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < 3; i++) if (knoppen[i].stabiel) n++;
  return n;
}

// Twee knoppen die kort samen ingedrukt staan -> mengkleur.
void checkCombo(uint32_t nu) {
  if (comboBezig) return;
  for (uint8_t p = 0; p < 3; p++) {
    uint8_t a = comboPaar[p][0], b = comboPaar[p][1];
    Knop &ka = knoppen[a], &kb = knoppen[b];
    if (ka.stabiel && kb.stabiel &&
        (nu - ka.neerMs) < LANG_DREMPEL_MS && (nu - kb.neerMs) < LANG_DREMPEL_MS) {
      uint32_t verschil = (ka.neerMs > kb.neerMs) ? ka.neerMs - kb.neerMs
                                                  : kb.neerMs - ka.neerMs;
      if (verschil <= COMBO_WINDOW_MS) {
        kiesKleur(comboKleur[p][0], comboKleur[p][1], comboKleur[p][2]);
        ka.consumed = kb.consumed = true;
        ka.taps = kb.taps = 0;
        comboBezig = true;
        return;
      }
    }
  }
}

// Lang vasthouden van één losse knop -> tijdelijke felheid (of witte flits).
void checkHold(uint32_t nu) {
  if (aantalIn() != 1) return;
  for (uint8_t i = 0; i < 3; i++) {
    Knop &k = knoppen[i];
    if (k.stabiel && !k.consumed && !k.isHold && (nu - k.neerMs) >= LANG_DREMPEL_MS) {
      k.isHold = true;
      k.taps   = 0;
      if (i == ROOD)       { if (!aan) herstelLaatste(); felheid = F_HOOG;   kiesNieuwDoel(); }
      else if (i == BLAUW) { if (!aan) herstelLaatste(); felheid = F_MIDDEN; kiesNieuwDoel(); }
      else                 { witFlits(); }   // WIT
    }
  }
}

// Korte tikken afhandelen + blauw enkel/dubbel beslissen.
void checkTaps(uint32_t nu) {
  for (uint8_t i = 0; i < 3; i++) {
    Knop &k = knoppen[i];
    if (!k.justUp) continue;

    if (k.consumed) {                     // hoorde bij een combo
      k.consumed = false;
    } else if (k.isHold) {                // einde van een lang-vasthouden
      k.isHold = false;
      if (i == ROOD || i == BLAUW) { felheid = F_ZACHT; kiesNieuwDoel(); }
    } else if (k.duur < LANG_DREMPEL_MS) { // korte tik
      if (i == ROOD)       kiesKleur(255, 0, 0);
      else if (i == WIT)   kiesKleur(255, 255, 255);
      else { k.taps++; k.laatsteTapMs = nu; }   // BLAUW: wacht op evt. 2e tik
    }
  }

  if (aantalIn() == 0) comboBezig = false;

  // Blauw: 2 tikken = blauw, 1 tik (na venster) = groen.
  Knop &bl = knoppen[BLAUW];
  if (bl.taps > 0 && !bl.stabiel) {
    if (bl.taps >= 2)                              { kiesKleur(0, 0, 255); bl.taps = 0; }
    else if ((nu - bl.laatsteTapMs) > DUBBEL_WINDOW_MS) { kiesKleur(0, 255, 0); bl.taps = 0; }
  }
}

// Kies een kleur. Dezelfde kleur opnieuw kiezen terwijl 'ie aan is -> uit.
void kiesKleur(uint8_t r, uint8_t g, uint8_t b) {
  if (aan && r == actR && g == actG && b == actB) { aan = false; return; }
  actR = r; actG = g; actB = b;
  lastR = r; lastG = g; lastB = b;
  aan = true;
  felheid = F_ZACHT;
  kiesNieuwDoel();
}

// Zet de laatst gekozen kleur weer aan (voor boost/flits vanuit uit-stand).
void herstelLaatste() {
  actR = lastR; actG = lastG; actB = lastB;
  aan = true;
}

// Eén korte felle flits in de huidige kleur, daarna uit.
void witFlits() {
  if (!aan) { actR = lastR; actG = lastG; actB = lastB; }
  toonRGB(actR, actG, actB, 255);
  delay(WIT_FLITS_MS);
  toonRGB(0, 0, 0, 0);
  aan = false;
  ademNu = 0;
  ademDoel = 0;
}

// Het {min,max}-bereik van de huidige felheid.
void huidigBereik(uint8_t &mn, uint8_t &mx) {
  switch (felheid) {
    case F_MIDDEN: mn = MIDDEN_MIN; mx = MIDDEN_MAX; break;
    case F_HOOG:   mn = HOOG_MIN;   mx = HOOG_MAX;   break;
    default:       mn = ZACHT_MIN;  mx = ZACHT_MAX;  break;
  }
}

void kiesNieuwDoel() {
  uint8_t mn, mx;
  huidigBereik(mn, mx);
  if (mx <= mn) { ademDoel = mn; return; }
  ademDoel = mn + random(mx - mn + 1);   // willekeurig binnen bereik -> onregelmatig
}

// Onregelmatige maar vloeiende adembeweging; schrijft naar de NeoPixel.
void ademLus(uint32_t nu) {
  if ((nu - ademVorigMs) < ADEM_STAP_MS) return;
  ademVorigMs = nu;

  if (!aan) {
    if (ademNu > 0) { ademNu--; schrijfPixel(); }   // zacht uitdimmen
    return;
  }

  if      (ademNu < ademDoel) ademNu++;
  else if (ademNu > ademDoel) ademNu--;
  else                        kiesNieuwDoel();

  schrijfPixel();
}

void schrijfPixel() {
  toonRGB(actR, actG, actB, ademNu);
}

// Toon een kleur op de pixel, geschaald met helderheid 0..255.
void toonRGB(uint8_t r, uint8_t g, uint8_t b, uint8_t helder) {
  uint8_t rr = (uint16_t)r * helder / 255;
  uint8_t gg = (uint16_t)g * helder / 255;
  uint8_t bb = (uint16_t)b * helder / 255;
  pixel.setPixelColor(0, pixel.Color(rr, gg, bb));
  pixel.show();
}
