# Staf — cosplay lichtstaf

Mini-firmware voor een ATtiny85 op 2× AA met **1 NeoPixel** en **3 drukknoppen**.
Per knopje een kleur, met een onregelmatig "ademend" gloei-effect in drie
helderheidsniveaus, gekozen via een drukpatroon.

## Hardware

| Functie       | Fysieke pin ATtiny85 | Arduino-pin | Opmerking            |
|---------------|----------------------|-------------|----------------------|
| Wit knopje    | 2                    | D3 (PB3)    | → wit licht          |
| Blauw knopje  | 3                    | D4 (PB4)    | → **groen** licht    |
| Rood knopje   | 7                    | D2 (PB2)    | → rood licht         |
| NeoPixel data | 6                    | D1 (PB1)    | 1 LED                |
| GND / VCC     | 4 / 8                | —           | 2× AA (≈3 V)         |

> Let op: je gaf bij het blauwe knopje per ongeluk "D3" op, maar fysieke pin 3
> is **D4 (PB4)**. Die heb ik gebruikt. Verwissel `PIN_BLAUW` in `staf.ino` als
> je het anders hebt gesoldeerd.

**Knoppen** zijn naar GND geschakeld en gebruiken de interne pull-up
(`INPUT_PULLUP`) — ingedrukt = LOW. Geen extra weerstanden nodig.

## Bediening

Per knopje (kleur = die van het knopje, blauw geeft groen):

| Patroon                | Effect                              |
|------------------------|-------------------------------------|
| 1× kort drukken         | zacht gloeien (**laag**)            |
| 1× lang vasthouden       | zelfde effect, veel meer licht (**hoog**) |
| kort, kort, lang        | zelfde idee op **volle sterkte**    |

Het licht wisselt onregelmatig harder/zachter, maar in stapjes van 1 zodat het
vloeiend blijft (geen geschok).

**Uitzetten:** druk 1× kort op de kleur die op dat moment aan staat (toggle).
Een ander knopje kiezen schakelt direct om naar die kleur.

"Kort" = losgelaten binnen 700 ms, "lang" = langer vastgehouden. Een patroon is
af zodra je 400 ms niets indrukt. Pas dit aan met `LANG_DREMPEL_MS` en
`GEBAAR_GAT_MS` boven in de sketch.

## Compileren & uploaden (Arduino IDE)

1. Installeer **ATTinyCore** (of damellis "attiny") via de Board Manager.
2. Installeer de bibliotheek **Adafruit NeoPixel**.
3. Board: **ATtiny25/45/85**, Chip: **ATtiny85**.
4. **Clock: 8 MHz (internal)** of 16 MHz (PLL). NeoPixels werken niet
   betrouwbaar op 1 MHz — kies dus 8 MHz en doe eenmalig **Bootloader branden**
   zodat de fuse goed staat.
5. Upload via je ISP-programmer (bv. USBasp of "Arduino as ISP").

Geverifieerd met `arduino-cli` (ATtiny85 @ 8 MHz): **3654 bytes flash (44%),
126 bytes RAM (24%)**.

## Afstellen

Boven in `staf.ino`:

- `NIV_LAAG`, `NIV_HOOG`, `NIV_MAX` — de `{min, max}` helderheidsbereiken per
  niveau (0–255). Pas aan naar smaak / batterijverbruik.
- `ADEM_STAP_MS` — lager = sneller ademen, hoger = trager/rustiger.
