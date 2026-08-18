#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

#include "protocol.h"

const int MIC_D0_PIN = 34;
const int TRANSITIONS_THRESHOLD = 100;

const int HUNT_MS = 5;
const int HUNT_THRESHOLD = 20;
const int RX_BUFFER_SIZE = 64;

uint8_t s_buffer[RX_BUFFER_SIZE];
int s_bufferLen = 0;

Adafruit_SSD1306 display(128, 64, &Wire, -1);

enum RxState
{
  HUNT,
  RECEIVING_PREAMBLE,
  RECEIVING_BIT
};

struct Window
{
  bool active;
  unsigned long startTime;
  uint32_t startEdges;
};

RxState s_rxState = HUNT;
Window s_window = Window{
    false,
    0,
    0,
};
int s_toneWindows = 0;
uint8_t s_receivedByte = 0;
bool s_needsRedraw = false;

volatile uint32_t s_edges = 0;

void IRAM_ATTR onMicEdge()
{
  s_edges++;
}

void startHuntWindow()
{
  s_window.active = true;
  s_window.startTime = millis();
  s_window.startEdges = s_edges;
}

void startPreambleWindow()
{
  s_window.active = true;
}

void startBitWindow()
{
  s_window.active = true;
  s_window.startTime = millis();
  s_window.startEdges = s_edges;
}

void incrementPreambleSlotCount(unsigned long currentTime)
{
  s_toneWindows++;
  s_window.startTime = currentTime;
  s_window.startEdges = s_edges;
}

void printWelcomeScreen()
{
  display.setCursor(0, 0);
  display.printf("Frequency: %d Hz\n", FREQ);
  display.printf("Slot duration: %d ms\n", SLOT_MS);
  display.printf("Rx ready !!!");
  display.display();
}

void displayBufferMsg()
{
  display.clearDisplay();
  display.setCursor(0, 0);

  for (int i = 0; i < s_bufferLen; i++)
  {
    display.print(isPrintable(s_buffer[i]) ? (char)s_buffer[i] : '.');
  }
  display.display();
}

void addToBuffer(uint8_t byte)
{
  if (s_bufferLen == RX_BUFFER_SIZE)
  {
    memmove(s_buffer, s_buffer + 1, RX_BUFFER_SIZE - 1);
    s_bufferLen--;
  }
  s_buffer[s_bufferLen++] = byte;
}

void setup()
{
  Serial.begin(115200);
  pinMode(MIC_D0_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(MIC_D0_PIN), onMicEdge, CHANGE);

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("SSD1306 not found");
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  printWelcomeScreen();
}

void loop()
{
  unsigned long currentTime = millis();
  switch (s_rxState)
  {
  case HUNT:
    if (s_needsRedraw)
    {
      displayBufferMsg();
      s_needsRedraw = false;
      break;
    }

    if (!s_window.active)
    {
      startHuntWindow();
    }
    else if (currentTime - s_window.startTime >= HUNT_MS)
    {
      // if hunt window is over, check if we have enough edges to consider it a preamble
      if (s_edges - s_window.startEdges >= HUNT_THRESHOLD)
      {
        s_rxState = RECEIVING_PREAMBLE;
      }
      // reset hunt window
      s_window.active = false;
    }
    break;

  case RECEIVING_PREAMBLE:
    if (!s_window.active)
    {
      startPreambleWindow();
    }

    // measuring slots amount instead of full preamble duration, to exit early if preamble stop receiveing signal
    if (s_window.active && currentTime - s_window.startTime >= SLOT_MS)
    {
      if (s_edges - s_window.startEdges >= TRANSITIONS_THRESHOLD)
      {
        incrementPreambleSlotCount(currentTime);
        if (s_toneWindows >= PREAMBLE_SLOTS_NEEDED)
        {
          s_rxState = RECEIVING_BIT;
          s_window.active = false;
          s_toneWindows = 0;
        }
      }
      else
      {
        s_rxState = HUNT;
        s_toneWindows = 0;
        s_window.active = false;
      }
    }

    break;

  case RECEIVING_BIT:
    if (!s_window.active)
    {
      startBitWindow();
    }

    if (s_window.active && currentTime - s_window.startTime >= SLOT_MS)
    {
      if (s_edges - s_window.startEdges >= TRANSITIONS_THRESHOLD)
      {
        s_receivedByte |= (1 << (7 - s_toneWindows));
      }

      s_toneWindows++;
      if (s_toneWindows >= BIT_SLOTS_NEEDED)
      {
        addToBuffer(s_receivedByte);
        Serial.printf("rx: 0x%02X\n", s_receivedByte);
        s_rxState = HUNT;
        s_toneWindows = 0;
        s_window.active = false;
        s_receivedByte = 0;
        s_needsRedraw = true;
      }
      else
      {
        // reset bit window for next bit
        s_window.startTime = currentTime;
        s_window.startEdges = s_edges;
      }
    }
    break;
  }
}
