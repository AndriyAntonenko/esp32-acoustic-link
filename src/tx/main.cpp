#include <Arduino.h>
#include "protocol.h"

const int BUZZER_PIN = 4;
const char *MESSAGE = "WHAT HATH GOD WROUGHT";

// -------- state --------
int s_wordIndex = 0;
bool s_isPreamblePlaying = false;
unsigned long s_preamblePlayStartedAt = 0;

// -------- functions --------
void playPreambleSync();
void playWordSync(uint8_t word);
void playWordWithPreambleSync(uint8_t word);

void setup()
{
  Serial.begin(115200);
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN);
  delay(200);
  tone(BUZZER_PIN, FREQ);
}

void loop()
{
  for (const char *p = MESSAGE; *p; p++)
  {
    playWordWithPreambleSync((uint8_t)*p);
    delay(50);
  }
  delay(3000);
}

void playPreambleSync()
{
  tone(BUZZER_PIN, FREQ);
  s_isPreamblePlaying = true;
  s_preamblePlayStartedAt = millis();

  while (millis() - s_preamblePlayStartedAt < PREAMBLE_MS)
  {
  }

  noTone(BUZZER_PIN);
  s_isPreamblePlaying = false;
}

void playWordSync(uint8_t word)
{
  unsigned long slotStartTime = millis();
  for (int i = 0; i < 8; i++)
  {
    int bit = (word >> (7 - i)) & 1;
    if (bit)
      tone(BUZZER_PIN, FREQ);
    else
      noTone(BUZZER_PIN);

    unsigned long nextSlotTime = slotStartTime + (unsigned long)(i + 1) * SLOT_MS;
    while (millis() < nextSlotTime)
    {
    }
  }
  noTone(BUZZER_PIN);
}

void playWordWithPreambleSync(uint8_t word)
{
  playPreambleSync();
  playWordSync(word);
}
