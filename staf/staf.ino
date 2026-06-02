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
 * ── OPSTARTCODE (vergrendeling) ────────────────────────────────────────────
 * Bij opstart is de staf vergrendeld. Code = ROOD, WIT, WIT, BLAUW. Direct na
 * inschakelen mag de code meteen worden ingevoerd. Een foute knop reset de
 * invoer en blokkeert nieuwe invoer tot er 10 s lang geen knop is ingedrukt.
 * Elke druk in vergrendelde staat geeft een korte zachte witte knippering
 * (150 ms, ~10%) als feedback.
 *
 * ── BEDIENING ─────────────────────────────────────────────────────────────
 * KORT drukken kiest een kleur (lamp gaat aan en "ademt" zacht en
 * onregelmatig):
 *   - rood  1x          -> ROOD
 *   - blauw 1x          -> meteen GROEN
 *   - blauw 2x (<1,5 s) -> eerst kort groen, dan door naar BLAUW
 *   - wit   1x          -> WIT
 * Twee knoppen samen (overlappend, starts binnen 1 s) mengt:
 *   - rood + blauw      -> PAARS
 *   - blauw + wit       -> GEEL
 *   - rood + wit        -> ORANJE
 * Nogmaals dezelfde kleur/combo kort drukken -> UIT (toggle).
 * LANG vasthouden = tijdelijke felheid in de HUIDIGE kleur (loslaten ->
 * terug naar zacht faden):
 *   - rood vast         -> faden tussen ~85% en 100%; loslaten = snel terug
 *   - blauw vast        -> faden tussen ~20% en 50%; loslaten = snel terug
 *   - wit vast          -> blijft flitsen (150 ms aan, 300 ms rust) zolang
 *                          ingedrukt; loslaten -> terug naar zacht faden.
 *                          Tijdens vasthouden: blauw tikken = langere rust,
 *                          rood tikken = kortere rust (live ritme regelen).
 * ALLE DRIE de knoppen samen (binnen 1 s, met overlap, niet tussendoor alles
 * los) -> WILLEKEURIGE-KLEUREN MODUS: de kleur dwaalt rustig willekeurig rond.
 *   - nogmaals 3 knoppen samen -> modus uit
 *   - een gewone kleurtik       -> stapt uit de modus naar die kleur
 *   - in deze modus: rood/blauw vast boost de kleur die op dat moment aan
 *     staat; wit vast laat elke flits een andere willekeurige kleur zien.
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
#define LANG_DREMPEL_MS  150     // >= dit ingedrukt = "lang vasthouden" (snelle reactie)
#define DUBBEL_WINDOW_MS 1500    // 2e tik op blauw moet binnen dit venster
#define COMBO_WINDOW_MS  1000    // twee knoppen samen: starts binnen dit venster
#define COMBO_VERS_MS    350     // combo: beide drukken moeten zo "vers" zijn (los van lange-klik)
#define WIT_FLITS_AAN_MS 150     // wit vasthouden: duur van elke flits
#define WIT_FLITS_UIT_MS 300     // wit vasthouden: rust tussen de flitsen (startwaarde)
#define WIT_RUST_STAP    50      // wit vasthouden: stap waarmee blauw/rood de rust aanpast
#define WIT_RUST_MIN     50      // kortste rust (rood verkort tot hier)
#define WIT_RUST_MAX     1500    // langste rust (blauw verlengt tot hier)
#define ADEM_STAP_MS     12      // tijd per helderheidsstapje (vloeiendheid)

// ---- Willekeurige-kleuren modus -------------------------------------------
#define WILLEKEUR_STAP_MS    25    // tempo van het kleur-kruipen (rustig)
#define WILLEKEUR_DOEL_MIN   2500  // min tijd voor een nieuwe doelkleur
#define WILLEKEUR_DOEL_EXTRA 2500  // + willekeurig tot dit erbovenop

// ---- Opstartcode (vergrendeling) ------------------------------------------
#define CODE_IDLE_MS      10000    // eerst zo lang niks indrukken voor de 1e codeknop telt
#define CODE_BLINK_MS     150      // duur van de feedback-knippering per druk
#define CODE_BLINK_HELDER 26       // ~10% wit
#define CODE_LENGTE       4

// ---- Felheidsbereiken {min,max} voor de adembeweging -----------------------
#define ZACHT_MIN   4
#define ZACHT_MAX   40
#define BLAUW_MIN   51    // ~20% van 255 (blauw vasthouden)
#define BLAUW_MAX   128   // ~50%
#define ROOD_MIN    217   // ~85% van 255 (rood vasthouden)
#define ROOD_MAX    255   // 100%
#define VOL_LICHT   255   // 100% vast (wit vasthouden)

enum Felheid { F_ZACHT, F_BLAUW, F_ROOD };

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

// Opstartcode: rood - wit - wit - blauw.
const uint8_t code[CODE_LENGTE] = { ROOD, WIT, WIT, BLAUW };

// ---- Actieve lichtstaat ----------------------------------------------------
bool     aan      = false;
uint8_t  actR = 255, actG = 0, actB = 0;   // huidige kleur
uint8_t  lastR = 255, lastG = 0, lastB = 0; // laatst gekozen kleur (voor boost/flits)
Felheid  felheid  = F_ZACHT;
bool     comboBezig = false;
bool     witBezig   = false;   // wit-knop: flits-ritme bezig
bool     witAanFase = false;   // huidige fase: true = aan, false = rust
uint32_t witStapEindMs = 0;    // moment waarop de huidige fase eindigt
uint16_t witRust    = WIT_FLITS_UIT_MS;  // actuele rust tussen flitsen (blauw/rood passen live aan)

// willekeurige-kleuren modus
bool     willekeurig = false;          // modus actief
uint8_t  doelR = 0, doelG = 0, doelB = 0; // kleur waar we rustig naartoe kruipen
uint32_t willekeurDoelMs = 0;          // moment voor een nieuwe doelkleur
uint32_t willekeurStapMs = 0;          // laatste kleur-kruipstap

// opstartcode / vergrendeling
bool     vergrendeld = true;   // start vergrendeld tot de code klopt
uint8_t  codePos = 0;          // aantal correcte code-stappen tot nu toe
bool     codeStraf = false;    // na foute knop: invoer geblokkeerd tot 10s stilte
uint32_t laatsteDrukMs = 0;    // moment van laatste knopdruk (voor de 10s-straf)
bool     codeBlink = false;    // feedback-knippering bezig
uint32_t codeBlinkEindMs = 0;

// 3-knops gebaar (sessie = vanaf eerste druk tot alles los)
bool     sessieActief = false;
uint8_t  sessieGezien = 0;             // bitmasker van knoppen die met overlap zijn gezien
uint32_t sessieStartMs = 0;
bool     drieGedaan = false;           // gebaar al getriggerd in deze sessie

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

  if (vergrendeld) {                 // functies pas vrij na de juiste opstartcode
    codeInvoer(nu);
    codeBlinkLus(nu);
    return;
  }

  witWachtAanpassen(nu);   // blauw/rood passen tijdens wit-flitsen de rust aan
  checkDrieKnops(nu);
  checkCombo(nu);
  checkHold(nu);
  checkWitFlits(nu);
  checkTaps(nu);
  willekeurigLus(nu);
  ademLus(nu);
}

// Tijdens wit vasthouden: blauw verlengt de rust, rood verkort 'm.
void witWachtAanpassen(uint32_t nu) {
  if (!witBezig) return;
  if (knoppen[BLAUW].justDown) {
    witRust += WIT_RUST_STAP;
    if (witRust > WIT_RUST_MAX) witRust = WIT_RUST_MAX;
    knoppen[BLAUW].consumed = true;       // geen combo/tik van deze druk
  }
  if (knoppen[ROOD].justDown) {
    witRust = (witRust >= WIT_RUST_MIN + WIT_RUST_STAP) ? witRust - WIT_RUST_STAP : WIT_RUST_MIN;
    knoppen[ROOD].consumed = true;
  }
}

// Toon de huidige fase: aan = vol licht, rust = uit; zet de eindtijd.
void witToonFase(uint32_t nu) {
  if (witAanFase) {
    if (willekeurig) wiel(random(256), actR, actG, actB);  // elke flits andere kleur
    toonRGB(actR, actG, actB, VOL_LICHT);
    witStapEindMs = nu + WIT_FLITS_AAN_MS;
  } else {
    toonRGB(0, 0, 0, 0);
    witStapEindMs = nu + witRust;
  }
}

// Wit vasthouden: blijf aan/rust afwisselen zolang ingedrukt (tot loslaten).
void checkWitFlits(uint32_t nu) {
  if (!witBezig) return;
  if ((int32_t)(nu - witStapEindMs) < 0) return;
  witAanFase = !witAanFase;
  witToonFase(nu);
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

// Opstartcode invoeren: rood-wit-wit-blauw, eerste knop pas na >=10s rust.
// Elke druk geeft een korte zachte witte feedback-knippering.
void codeInvoer(uint32_t nu) {
  for (uint8_t i = 0; i < 3; i++) {
    if (!knoppen[i].justDown) continue;

    codeBlink = true;                          // feedback: 150 ms zacht wit
    codeBlinkEindMs = nu + CODE_BLINK_MS;
    toonRGB(255, 255, 255, CODE_BLINK_HELDER);

    uint32_t gap = nu - laatsteDrukMs;                     // rust sinds vorige druk
    laatsteDrukMs = nu;
    if (codeStraf && gap >= CODE_IDLE_MS) codeStraf = false; // 10s stilte -> straf voorbij

    if (codeStraf) continue;                               // straf actief: nog niks invoeren (wel feedback)

    if (codePos == 0) {
      if (i == code[0]) codePos = 1;                       // juiste start (mag direct na opstart)
      else              codeStraf = true;                  // foute eerste knop -> 10s straf
    } else if (i == code[codePos]) {
      codePos++;
      if (codePos >= CODE_LENGTE) {                        // ontgrendeld
        vergrendeld = false;
        codePos = 0;
        codeBlink = false;
        toonRGB(0, 0, 0, 0);
      }
    } else {
      codePos = 0;                                         // fout -> opnieuw, met 10s straf
      codeStraf = true;
    }
  }
}

void codeBlinkLus(uint32_t nu) {
  if (codeBlink && (int32_t)(nu - codeBlinkEindMs) >= 0) {
    codeBlink = false;
    toonRGB(0, 0, 0, 0);
  }
}

// Twee knoppen die kort samen ingedrukt staan -> mengkleur.
void checkCombo(uint32_t nu) {
  if (comboBezig || witBezig) return;   // niet tijdens wit-flitsen (blauw/rood regelen dan de rust)
  for (uint8_t p = 0; p < 3; p++) {
    uint8_t a = comboPaar[p][0], b = comboPaar[p][1];
    Knop &ka = knoppen[a], &kb = knoppen[b];
    if (ka.stabiel && kb.stabiel &&
        (nu - ka.neerMs) < COMBO_VERS_MS && (nu - kb.neerMs) < COMBO_VERS_MS) {
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
      if (i == ROOD)       { if (!aan) herstelLaatste(); felheid = F_ROOD;  kiesNieuwDoel(); }
      else if (i == BLAUW) { if (!aan) herstelLaatste(); felheid = F_BLAUW; kiesNieuwDoel(); }
      else {                                                              // WIT
        if (!aan) herstelLaatste();
        aan = true;
        witBezig = true;
        witAanFase = true;            // begin met een flits
        witRust = WIT_FLITS_UIT_MS;   // elke nieuwe hold start op de standaard-rust
        witToonFase(nu);
      }
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
      witBezig = false;
      aan = true;                         // hervat (wit kan tijdens de flitsen uit zijn)
      felheid = F_ZACHT;
      ademNu = ZACHT_MAX;                 // meteen snel terug naar lage sterkte
      kiesNieuwDoel();
    } else if (k.duur < LANG_DREMPEL_MS) { // korte tik
      if (i == ROOD)       kiesKleur(255, 0, 0);
      else if (i == WIT)   kiesKleur(255, 255, 255);
      else {                                       // BLAUW
        if (k.taps == 1 && (nu - k.laatsteTapMs) <= DUBBEL_WINDOW_MS) {
          kiesKleur(0, 0, 255);   // snelle 2e tik -> door naar blauw
          k.taps = 0;
        } else {
          kiesKleur(0, 255, 0);   // 1e tik -> meteen groen
          k.taps = 1;
          k.laatsteTapMs = nu;
        }
      }
    }
  }

  if (aantalIn() == 0) comboBezig = false;
}

// Kies een kleur. Dezelfde kleur opnieuw kiezen terwijl 'ie aan is -> uit.
void kiesKleur(uint8_t r, uint8_t g, uint8_t b) {
  willekeurig = false;   // een bewuste kleurkeuze stapt uit de willekeur-modus
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

// Het {min,max}-bereik van de huidige felheid.
void huidigBereik(uint8_t &mn, uint8_t &mx) {
  switch (felheid) {
    case F_BLAUW: mn = BLAUW_MIN; mx = BLAUW_MAX; break;
    case F_ROOD:  mn = ROOD_MIN;  mx = ROOD_MAX;  break;
    default:      mn = ZACHT_MIN; mx = ZACHT_MAX; break;
  }
}

void kiesNieuwDoel() {
  uint8_t mn, mx;
  huidigBereik(mn, mx);
  if (mx <= mn) { ademDoel = mn; return; }
  ademDoel = mn + random(mx - mn + 1);   // willekeurig binnen bereik -> onregelmatig
}

// Kleurenwiel: verzadigde kleur voor positie 0..255 (Adafruit-stijl).
void wiel(uint8_t pos, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (pos < 85)        { r = pos * 3;          g = 255 - pos * 3;    b = 0; }
  else if (pos < 170)  { pos -= 85;  r = 255 - pos * 3; g = 0;       b = pos * 3; }
  else                 { pos -= 170; r = 0;            g = pos * 3;  b = 255 - pos * 3; }
}

void kiesWillekeurigDoel() {
  wiel(random(256), doelR, doelG, doelB);
}

// Start/stop de willekeurige-kleuren modus (3-knops gebaar).
void drieKnopsGebaar(uint32_t nu) {
  willekeurig = !willekeurig;
  if (willekeurig) {
    aan = true;
    felheid = F_ZACHT;
    wiel(random(256), actR, actG, actB);   // begin op een willekeurige kleur
    doelR = actR; doelG = actG; doelB = actB;
    willekeurDoelMs = nu + WILLEKEUR_DOEL_MIN;
    willekeurStapMs = nu;
    kiesNieuwDoel();
  } else {
    aan = false;
  }
}

// Detecteer "alle 3 binnen 1 s, met overlap, niet tussendoor alles los".
void checkDrieKnops(uint32_t nu) {
  if (witBezig) return;                 // niet tijdens wit-flitsen (blauw/rood regelen dan de rust)
  uint8_t n = aantalIn();
  if (n == 0) { sessieActief = false; sessieGezien = 0; drieGedaan = false; return; }

  if (!sessieActief) { sessieActief = true; sessieStartMs = nu; sessieGezien = 0; drieGedaan = false; }

  if (n >= 2)   // op dit moment overlappen knoppen -> markeer de ingedrukte
    for (uint8_t i = 0; i < 3; i++) if (knoppen[i].stabiel) sessieGezien |= (1 << i);

  if (!drieGedaan && sessieGezien == 0x07 && (nu - sessieStartMs) <= COMBO_WINDOW_MS) {
    drieGedaan = true;
    for (uint8_t i = 0; i < 3; i++) { knoppen[i].consumed = true; knoppen[i].taps = 0; }
    comboBezig = true;            // voorkom losse 2-combo's deze sessie
    drieKnopsGebaar(nu);
  }
}

// Willekeurige-kleuren modus: rustig naar steeds nieuwe doelkleuren kruipen.
void willekeurigLus(uint32_t nu) {
  if (!willekeurig) return;
  if (witBezig || knoppen[ROOD].isHold || knoppen[BLAUW].isHold) return;  // boost/flits: kleur bevriezen

  if ((int32_t)(nu - willekeurDoelMs) >= 0) {
    kiesWillekeurigDoel();
    willekeurDoelMs = nu + WILLEKEUR_DOEL_MIN + random(WILLEKEUR_DOEL_EXTRA);
  }

  if ((nu - willekeurStapMs) >= WILLEKEUR_STAP_MS) {
    willekeurStapMs = nu;
    if (actR < doelR) actR++; else if (actR > doelR) actR--;
    if (actG < doelG) actG++; else if (actG > doelG) actG--;
    if (actB < doelB) actB++; else if (actB > doelB) actB--;
  }
}

// Onregelmatige maar vloeiende adembeweging; schrijft naar de NeoPixel.
void ademLus(uint32_t nu) {
  if (witBezig) return;                  // tijdens de flits-reeks niets faden
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
