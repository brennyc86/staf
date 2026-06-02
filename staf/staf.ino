/*
 * STAF — cosplay lichtstaf
 * --------------------------------------------------------------------------
 * ATtiny85 op 2x AA, 1 NeoPixel + 3 drukknoppen.
 *
 * Hardware (fysieke pin -> Arduino pin in ATTinyCore):
 *   - Wit knopje    : fysiek pin 2  = D3
 *   - Blauw knopje  : fysiek pin 3  = D4   (gaf GROEN licht, op verzoek)
 *   - Rood knopje   : fysiek pin 7  = D2
 *   - NeoPixel data : fysiek pin 5  = D0
 *
 * Knoppen zijn naar GND geschakeld en gebruiken de interne pull-up
 * (INPUT_PULLUP): ingedrukt = LOW. Geen extra weerstanden nodig.
 *
 * BELANGRIJK: NeoPixels hebben strakke timing nodig. Zet de ATtiny85 op
 * 8 MHz (intern) of 16 MHz (PLL) via Tools -> Clock en daarna eenmalig
 * "Bootloader branden". Op 1 MHz werkt de NeoPixel NIET betrouwbaar.
 *
 * Bediening (per knopje, kleur = die van het knopje, blauw->groen):
 *   - 1x kort drukken        -> zacht gloeien (laag)
 *   - 1x lang vasthouden      -> zelfde effect, veel meer licht (hoog)
 *   - kort, kort, lang        -> zelfde idee op volle sterkte (max)
 * Het licht "ademt" onregelmatig harder/zachter, maar in stapjes van 1
 * zodat het vloeiend blijft.
 *
 * Uitzetten: druk 1x kort op de kleur die op dat moment AAN staat
 * (laag-niveau toggle). Een andere kleur kiezen schakelt direct om.
 */

#include <Adafruit_NeoPixel.h>

// ---- Pinnen ----------------------------------------------------------------
#define PIN_WIT     3   // fysiek pin 2
#define PIN_BLAUW   4   // fysiek pin 3  -> groen
#define PIN_ROOD    2   // fysiek pin 7
#define PIN_PIXEL   0   // fysiek pin 5

// ---- Tijden ----------------------------------------------------------------
#define DEBOUNCE_MS      25     // ontdender
#define LANG_DREMPEL_MS  700    // >= dit ingedrukt = "lang", anders "kort"
#define GEBAAR_GAT_MS    400    // stilte na loslaten -> gebaar is af
#define ADEM_STAP_MS     12     // tijd per helderheidsstapje (vloeiendheid)

// ---- Kleuren (basis, wordt geschaald met de adem-helderheid) ---------------
const uint8_t KLEUR_WIT[3]   = {255, 255, 255};
const uint8_t KLEUR_GROEN[3] = {  0, 255,   0};
const uint8_t KLEUR_ROOD[3]  = {255,   0,   0};

// ---- Helderheidsniveaus: {min, max} voor de adembeweging -------------------
const uint8_t NIV_LAAG[2] = {  6,  45};
const uint8_t NIV_HOOG[2] = { 70, 170};
const uint8_t NIV_MAX[2]  = {200, 255};

enum Niveau { NIV_UIT, NIV_L, NIV_H, NIV_M };

Adafruit_NeoPixel pixel(1, PIN_PIXEL, NEO_GRB + NEO_KHZ800);

// ---- Knop-/gebaarstatus per knop -------------------------------------------
struct Knop {
  uint8_t  pin;
  const uint8_t *kleur;
  // ontdender
  bool     stabiel;        // true = ingedrukt
  bool     vorigeMeting;
  uint32_t veranderMs;
  // gebaaropbouw
  uint8_t  kort;           // aantal korte drukken in dit gebaar
  uint8_t  lang;           // aantal lange drukken in dit gebaar
  bool     laatsteLang;    // was de laatste druk lang?
  uint32_t drukStartMs;    // moment van indrukken
  uint32_t laatsteLosMs;   // moment van laatste loslaten
  bool     bezig;          // er loopt een gebaar
};

Knop knoppen[3] = {
  {PIN_WIT,   KLEUR_WIT,   false, false, 0, 0, 0, false, 0, 0, false},
  {PIN_BLAUW, KLEUR_GROEN, false, false, 0, 0, 0, false, 0, 0, false},
  {PIN_ROOD,  KLEUR_ROOD,  false, false, 0, 0, 0, false, 0, 0, false},
};

// ---- Actieve lichtstaat ----------------------------------------------------
const uint8_t *actieveKleur = KLEUR_WIT;
Niveau         actiefNiveau = NIV_UIT;

// adembeweging
uint8_t  ademNu     = 0;     // huidige helderheid
uint8_t  ademDoel   = 0;     // helderheid waar we naartoe kruipen
uint32_t ademVorigMs = 0;

void setup() {
  for (uint8_t i = 0; i < 3; i++) {
    pinMode(knoppen[i].pin, INPUT_PULLUP);
    knoppen[i].vorigeMeting = (digitalRead(knoppen[i].pin) == LOW);
    knoppen[i].stabiel      = knoppen[i].vorigeMeting;
  }
  pixel.begin();
  pixel.clear();
  pixel.show();
  startFlits();              // visuele zelftest: rood -> groen -> wit
  randomSeed(analogRead(0));
}

// Korte kleurtest bij opstarten. Bevestigt dat de WS2812 werkt en dat de
// kleurvolgorde (GRB) klopt: je hoort rood, dan groen, dan wit te zien.
void startFlits() {
  const uint8_t test[3][3] = {
    {255,   0,   0},   // rood
    {  0, 255,   0},   // groen
    {255, 255, 255},   // wit
  };
  for (uint8_t i = 0; i < 3; i++) {
    pixel.setPixelColor(0, pixel.Color(test[i][0], test[i][1], test[i][2]));
    pixel.show();
    delay(250);
    pixel.clear();
    pixel.show();
    delay(120);
  }
}

void loop() {
  uint32_t nu = millis();
  for (uint8_t i = 0; i < 3; i++) verwerkKnop(i, nu);
  ademLus(nu);
}

// Ontdender + gebaarherkenning voor één knop.
void verwerkKnop(uint8_t i, uint32_t nu) {
  Knop &k = knoppen[i];

  bool meting = (digitalRead(k.pin) == LOW);   // LOW = ingedrukt
  if (meting != k.vorigeMeting) {
    k.vorigeMeting = meting;
    k.veranderMs = nu;
  }
  if ((nu - k.veranderMs) >= DEBOUNCE_MS && meting != k.stabiel) {
    k.stabiel = meting;
    if (meting) {
      // ingedrukt
      k.drukStartMs = nu;
      k.bezig = true;
    } else {
      // losgelaten -> klassificeer kort/lang
      uint32_t duur = nu - k.drukStartMs;
      if (duur >= LANG_DREMPEL_MS) { k.lang++; k.laatsteLang = true; }
      else                         { k.kort++; k.laatsteLang = false; }
      k.laatsteLosMs = nu;
    }
  }

  // Gebaar afronden: knop los en stilte voorbij.
  if (k.bezig && !k.stabiel && (k.kort + k.lang) > 0 &&
      (nu - k.laatsteLosMs) >= GEBAAR_GAT_MS) {
    rondGebaarAf(k);
    k.kort = 0; k.lang = 0; k.laatsteLang = false; k.bezig = false;
  }
}

// Vertaal het herkende gebaar naar een lichtstaat.
void rondGebaarAf(Knop &k) {
  Niveau gewenst = NIV_UIT;

  if (k.kort == 1 && k.lang == 0)                         gewenst = NIV_L;  // 1x kort
  else if (k.kort == 0 && k.lang == 1)                    gewenst = NIV_H;  // 1x lang
  else if (k.kort == 2 && k.lang == 1 && k.laatsteLang)   gewenst = NIV_M;  // kort,kort,lang
  else return;  // onbekend gebaar: negeren

  // 1x kort op de reeds actieve kleur -> uitzetten (toggle).
  if (gewenst == NIV_L && actiefNiveau != NIV_UIT && actieveKleur == k.kleur) {
    actiefNiveau = NIV_UIT;
    return;
  }

  actieveKleur = k.kleur;
  actiefNiveau = gewenst;
  kiesNieuwDoel();   // meteen een vers ademdoel binnen het nieuwe bereik
}

// Geef het {min,max}-bereik van het huidige niveau.
void huidigBereik(uint8_t &mn, uint8_t &mx) {
  switch (actiefNiveau) {
    case NIV_L: mn = NIV_LAAG[0]; mx = NIV_LAAG[1]; break;
    case NIV_H: mn = NIV_HOOG[0]; mx = NIV_HOOG[1]; break;
    case NIV_M: mn = NIV_MAX[0];  mx = NIV_MAX[1];  break;
    default:    mn = 0; mx = 0;   break;
  }
}

void kiesNieuwDoel() {
  uint8_t mn, mx;
  huidigBereik(mn, mx);
  if (mx <= mn) { ademDoel = mn; return; }
  ademDoel = mn + random(mx - mn + 1);   // willekeurig binnen bereik -> onregelmatig
}

// Onregelmatige, maar vloeiende adembeweging; schrijft naar de NeoPixel.
void ademLus(uint32_t nu) {
  if ((nu - ademVorigMs) < ADEM_STAP_MS) return;
  ademVorigMs = nu;

  if (actiefNiveau == NIV_UIT) {
    if (ademNu > 0) ademNu--;            // zacht uitdimmen
    schrijfPixel();
    return;
  }

  // Kruip in stapjes van 1 naar het doel; bij aankomst nieuw willekeurig doel.
  if      (ademNu < ademDoel) ademNu++;
  else if (ademNu > ademDoel) ademNu--;
  else                        kiesNieuwDoel();

  schrijfPixel();
}

void schrijfPixel() {
  uint16_t b = ademNu;   // 0..255
  uint8_t r = (uint16_t)actieveKleur[0] * b / 255;
  uint8_t g = (uint16_t)actieveKleur[1] * b / 255;
  uint8_t bl= (uint16_t)actieveKleur[2] * b / 255;
  pixel.setPixelColor(0, pixel.Color(r, g, bl));
  pixel.show();
}
