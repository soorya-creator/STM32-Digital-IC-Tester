/*
 * IC Tester — STM32F401CCUx (BlackPill)
 * OLED-DEBUG VERSION — all debug output goes to OLED, no Serial dependency
 */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>

// ─── OLED ──────────────────────────────────────────────────────────────────
#define OLED_WIDTH  128
#define OLED_HEIGHT  64
#define OLED_ADDR   0x3C

Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool oledOK = false;

// ─── PINS ──────────────────────────────────────────────────────────────────
const uint8_t IN_PINS[8]  = { PA0, PA1, PA2, PA3, PA4, PA5, PA6, PA7 };
const uint8_t OUT_PINS[4] = { PB0, PB1, PB2, PB3 };

#define LED_PASS  PA12
#define LED_FAIL  PB4    // changed from PA15 to avoid JTDI conflict

// ─── KEYPAD ────────────────────────────────────────────────────────────────
const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = { PB12, PB13, PB14, PB15 };
byte colPins[COLS] = { PA8,  PA9,  PA10, PA11  };
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// ─── IC DATABASE ───────────────────────────────────────────────────────────
struct ICTest {
  const char* name;
  void (*runTest)();
};

void test_74HC00();
void test_74HC02();
void test_74HC04();
void test_74HC08();
void test_74HC32();
void test_74HC86();
void test_74HC138();
void test_74HC245();

const ICTest icList[] = {
  { "74HC00  NAND",  test_74HC00  },
  { "74HC02  NOR",   test_74HC02  },
  { "74HC04  NOT",   test_74HC04  },
  { "74HC08  AND",   test_74HC08  },
  { "74HC32  OR",    test_74HC32  },
  { "74HC86  XOR",   test_74HC86  },
  { "74HC138 DEC",   test_74HC138 },
  { "74HC245 BUF",   test_74HC245 },
};
const uint8_t IC_COUNT = sizeof(icList) / sizeof(icList[0]);

int selectedIC = -1;

// ─── OLED HELPERS ──────────────────────────────────────────────────────────
void oledClear() {
  if (!oledOK) return;
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
}

void oledPrint(const char* line) {
  if (!oledOK) return;
  display.println(line);
}

void oledPrintBig(const char* line) {
  if (!oledOK) return;
  display.setTextSize(2);
  display.println(line);
  display.setTextSize(1);
}

void oledShow() {
  if (!oledOK) return;
  display.display();
}

// Show a full debug screen and wait for keypress to continue
void oledDebugScreen(const char* title,
                     const char* l1 = nullptr,
                     const char* l2 = nullptr,
                     const char* l3 = nullptr,
                     const char* l4 = nullptr,
                     bool waitKey = true) {
  oledClear();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println(title);
  display.drawLine(0, 9, 127, 9, SSD1306_WHITE);
  display.setCursor(0, 12);

  if (l1) display.println(l1);
  if (l2) display.println(l2);
  if (l3) display.println(l3);
  if (l4) display.println(l4);

  if (waitKey) {
    display.setCursor(0, 56);
    display.print("* continue...");
  }
  display.display();

  if (waitKey) {
    while (true) {
      char k = keypad.getKey();
      if (k) break;
    }
  }
}

// ─── RESULT FEEDBACK ───────────────────────────────────────────────────────
void showResult(bool pass, const char* details = nullptr) {
  digitalWrite(LED_PASS, pass ? HIGH : LOW);
  digitalWrite(LED_FAIL, pass ? LOW  : HIGH);

  oledClear();
  oledPrintBig(pass ? "PASS" : "FAIL");
  oledPrint(icList[selectedIC].name);
  if (details) oledPrint(details);
  oledShow();

  delay(3000);
  digitalWrite(LED_PASS, LOW);
  digitalWrite(LED_FAIL, LOW);
}

// ─── PIN HELPERS ───────────────────────────────────────────────────────────
void setPinModes() {
  for (auto p : IN_PINS)  pinMode(p, OUTPUT);
  for (auto p : OUT_PINS) pinMode(p, INPUT);
}

void driveInputs(uint8_t vals, uint8_t count = 8) {
  for (uint8_t i = 0; i < count; i++)
    digitalWrite(IN_PINS[i], (vals >> i) & 1 ? HIGH : LOW);
}

uint8_t readOutputs(uint8_t count = 4) {
  uint8_t result = 0;
  for (uint8_t i = 0; i < count; i++)
    if (digitalRead(OUT_PINS[i]) == HIGH) result |= (1 << i);
  return result;
}

// ─── OLED DEBUG HELPERS ────────────────────────────────────────────────────
void debugFloatCheck() {
  uint8_t raw = readOutputs(4);
  char line1[22], line2[22], line3[22], line4[22];

  snprintf(line1, sizeof(line1), "PB0=%d PB1=%d",
           digitalRead(OUT_PINS[0]), digitalRead(OUT_PINS[1]));
  snprintf(line2, sizeof(line2), "PB2=%d PB3=%d",
           digitalRead(OUT_PINS[2]), digitalRead(OUT_PINS[3]));
  snprintf(line3, sizeof(line3), "Raw=0x%X", raw);

  if (raw == 0x0F)
    snprintf(line4, sizeof(line4), "WARN:all HIGH!");
  else if (raw == 0x00)
    snprintf(line4, sizeof(line4), "OK: all LOW");
  else
    snprintf(line4, sizeof(line4), "MIXED state");

  oledDebugScreen("FloatChk(noIC)", line1, line2, line3, line4, true);
}

void debugOneRow(const char* icName,
                 uint8_t inputs,
                 uint8_t expected,
                 uint8_t got) {
  char l1[22], l2[22], l3[22], l4[22];
  snprintf(l1, sizeof(l1), "IC: %s", icName);

  snprintf(l2, sizeof(l2), "IN :%d%d%d%d %d%d%d%d",
           (inputs>>7)&1,(inputs>>6)&1,(inputs>>5)&1,(inputs>>4)&1,
           (inputs>>3)&1,(inputs>>2)&1,(inputs>>1)&1,(inputs>>0)&1);

  snprintf(l3, sizeof(l3), "EXP:%d%d%d%d GOT:%d%d%d%d",
           (expected>>3)&1,(expected>>2)&1,(expected>>1)&1,(expected>>0)&1,
           (got>>3)&1,    (got>>2)&1,    (got>>1)&1,    (got>>0)&1);

  snprintf(l4, sizeof(l4), "%s",
           (got == expected) ? "ROW: OK" : "ROW: FAIL!");

  oledDebugScreen("RowDebug", l1, l2, l3, l4, true);
}

void debugWiringCheck() {
  oledDebugScreen("WiringCheck",
                  "Will drive each",
                  "IN pin HIGH one",
                  "by one. Watch",
                  "OUT pins change.", true);

  for (uint8_t i = 0; i < 8; i++) {
    for (uint8_t j = 0; j < 8; j++)
      digitalWrite(IN_PINS[j], (j == i) ? HIGH : LOW);

    delay(100);
    uint8_t raw = readOutputs(4);

    char l1[22], l2[22], l3[22];
    snprintf(l1, sizeof(l1), "IN_PIN[%d]=PA%d HIGH", i, i);
    snprintf(l2, sizeof(l2), "OUT raw=0x%X", raw);
    snprintf(l3, sizeof(l3), "PB0=%d 1=%d 2=%d 3=%d",
             digitalRead(OUT_PINS[0]), digitalRead(OUT_PINS[1]),
             digitalRead(OUT_PINS[2]), digitalRead(OUT_PINS[3]));

    oledDebugScreen("WiringChk", l1, l2, l3, "* = next pin", true);
  }

  for (auto p : IN_PINS) digitalWrite(p, LOW);
}

// ─── IC TESTS ──────────────────────────────────────────────────────────────
void test_74HC00() {
  setPinModes();
  debugFloatCheck();
  bool pass = true;
  char detail[40];

  struct { uint8_t inputs; uint8_t expected; } tt[] = {
    {0b00000000, 0b1111},
    {0b01010101, 0b1111},
    {0b10101010, 0b1111},
    {0b11111111, 0b0000},
  };

  for (auto& t : tt) {
    driveInputs(t.inputs);
    delayMicroseconds(500);
    uint8_t got = readOutputs(4);
    debugOneRow("HC00", t.inputs, t.expected, got);
    if (got != t.expected) {
      pass = false;
      snprintf(detail, sizeof(detail), "in=%02X e=%X g=%X",
               t.inputs, t.expected, got);
      break;
    }
  }
  showResult(pass, pass ? nullptr : detail);
}

void test_74HC02() {
  setPinModes();
  debugFloatCheck();
  bool pass = true;
  char detail[40];

  struct { uint8_t inputs; uint8_t expected; } tt[] = {
    {0b00000000, 0b1111},
    {0b01010101, 0b0000},
    {0b10101010, 0b0000},
    {0b11111111, 0b0000},
  };

  for (auto& t : tt) {
    driveInputs(t.inputs);
    delayMicroseconds(500);
    uint8_t got = readOutputs(4);
    debugOneRow("HC02", t.inputs, t.expected, got);
    if (got != t.expected) {
      pass = false;
      snprintf(detail, sizeof(detail), "in=%02X e=%X g=%X",
               t.inputs, t.expected, got);
      break;
    }
  }
  showResult(pass, pass ? nullptr : detail);
}

void test_74HC04() {
  setPinModes();
  debugFloatCheck();
  bool pass = true;
  char detail[40];

  struct { uint8_t inputs; uint8_t expected; } tt[] = {
    {0b00000000, 0b1111},
    {0b00001111, 0b0000},
  };

  for (auto& t : tt) {
    for (uint8_t i = 0; i < 4; i++)
      digitalWrite(IN_PINS[i], (t.inputs >> i) & 1 ? HIGH : LOW);
    delayMicroseconds(500);
    uint8_t got = readOutputs(4);
    debugOneRow("HC04", t.inputs, t.expected, got);
    if (got != t.expected) {
      pass = false;
      snprintf(detail, sizeof(detail), "in=%X e=%X g=%X",
               t.inputs, t.expected, got);
      break;
    }
  }
  showResult(pass, pass ? nullptr : detail);
}

void test_74HC08() {
  setPinModes();
  debugFloatCheck();
  bool pass = true;
  char detail[40];

  struct { uint8_t inputs; uint8_t expected; } tt[] = {
    {0b00000000, 0b0000},
    {0b01010101, 0b0000},
    {0b10101010, 0b0000},
    {0b11111111, 0b1111},
  };

  for (auto& t : tt) {
    driveInputs(t.inputs);
    delayMicroseconds(500);
    uint8_t got = readOutputs(4);
    debugOneRow("HC08", t.inputs, t.expected, got);
    if (got != t.expected) {
      pass = false;
      snprintf(detail, sizeof(detail), "in=%02X e=%X g=%X",
               t.inputs, t.expected, got);
      break;
    }
  }
  showResult(pass, pass ? nullptr : detail);
}

void test_74HC32() {
  setPinModes();
  debugFloatCheck();
  bool pass = true;
  char detail[40];

  struct { uint8_t inputs; uint8_t expected; } tt[] = {
    {0b00000000, 0b0000},
    {0b01010101, 0b1111},
    {0b10101010, 0b1111},
    {0b11111111, 0b1111},
  };

  for (auto& t : tt) {
    driveInputs(t.inputs);
    delayMicroseconds(500);
    uint8_t got = readOutputs(4);
    debugOneRow("HC32", t.inputs, t.expected, got);
    if (got != t.expected) {
      pass = false;
      snprintf(detail, sizeof(detail), "in=%02X e=%X g=%X",
               t.inputs, t.expected, got);
      break;
    }
  }
  showResult(pass, pass ? nullptr : detail);
}

void test_74HC86() {
  setPinModes();
  debugFloatCheck();
  bool pass = true;
  char detail[40];

  struct { uint8_t inputs; uint8_t expected; } tt[] = {
    {0b00000000, 0b0000},
    {0b01010101, 0b1111},
    {0b10101010, 0b1111},
    {0b11111111, 0b0000},
  };

  for (auto& t : tt) {
    driveInputs(t.inputs);
    delayMicroseconds(500);
    uint8_t got = readOutputs(4);
    debugOneRow("HC86", t.inputs, t.expected, got);
    if (got != t.expected) {
      pass = false;
      snprintf(detail, sizeof(detail), "in=%02X e=%X g=%X",
               t.inputs, t.expected, got);
      break;
    }
  }
  showResult(pass, pass ? nullptr : detail);
}

void test_74HC138() {
  setPinModes();
  debugFloatCheck();
  bool pass = true;
  char detail[40];

  digitalWrite(IN_PINS[3], HIGH);
  digitalWrite(IN_PINS[4], LOW);
  digitalWrite(IN_PINS[5], LOW);
  delayMicroseconds(500);

  uint8_t expected[4] = { 0b1110, 0b1101, 0b1011, 0b0111 };

  for (uint8_t addr = 0; addr < 4; addr++) {
    digitalWrite(IN_PINS[0], (addr >> 0) & 1);
    digitalWrite(IN_PINS[1], (addr >> 1) & 1);
    digitalWrite(IN_PINS[2], (addr >> 2) & 1);
    delayMicroseconds(500);
    uint8_t got = readOutputs(4);
    debugOneRow("HC138", addr, expected[addr], got);
    if (got != expected[addr]) {
      pass = false;
      snprintf(detail, sizeof(detail), "addr=%d e=%X g=%X",
               addr, expected[addr], got);
      break;
    }
  }
  showResult(pass, pass ? nullptr : detail);
}

void test_74HC245() {
  setPinModes();
  debugFloatCheck();
  bool pass = true;
  char detail[40];

  digitalWrite(IN_PINS[0], LOW);
  digitalWrite(IN_PINS[1], LOW);
  delayMicroseconds(500);

  uint8_t testVals[] = { 0b0000, 0b1010, 0b0101, 0b1111 };

  for (uint8_t v : testVals) {
    for (uint8_t i = 0; i < 4; i++)
      digitalWrite(IN_PINS[i + 2], (v >> i) & 1 ? HIGH : LOW);
    delayMicroseconds(500);
    uint8_t got = readOutputs(4);
    debugOneRow("HC245", v, v, got);
    if (got != v) {
      pass = false;
      snprintf(detail, sizeof(detail), "in=%X e=%X g=%X", v, v, got);
      break;
    }
  }
  showResult(pass, pass ? nullptr : detail);
}

// ─── MENU ──────────────────────────────────────────────────────────────────
void showMenu() {
  if (!oledOK) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("=== IC Tester ===");
  for (uint8_t i = 0; i < IC_COUNT; i++) {
    char line[22];
    snprintf(line, sizeof(line), "%d:%s", i + 1, icList[i].name);
    display.println(line);
  }
  display.println("#=Run *=Back");
  display.display();
}

void showSelected() {
  if (!oledOK) return;
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Selected:");
  display.setTextSize(1);
  display.println(icList[selectedIC].name);
  display.println("");
  display.println("# = Test");
  display.println("* = Menu");
  display.println("A = WiringCheck");
  display.display();
}

// ─── SETUP ─────────────────────────────────────────────────────────────────
void setup() {
  // I2C init — explicit pins for BlackPill
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();
  Wire.setClock(100000); // 100kHz — stable for breadboard

  // LEDs
  pinMode(LED_PASS, OUTPUT);
  pinMode(LED_FAIL, OUTPUT);
  digitalWrite(LED_PASS, LOW);
  digitalWrite(LED_FAIL, LOW);

  // OLED init
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    oledOK = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("IC Tester");
    display.println("DEBUG MODE");
    display.println("OLED OK");
    display.display();
    delay(1000);
  }

  // LED blink test
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PASS, HIGH); delay(150);
    digitalWrite(LED_PASS, LOW);
    digitalWrite(LED_FAIL, HIGH); delay(150);
    digitalWrite(LED_FAIL, LOW);
  }

  showMenu();
}

// ─── LOOP ──────────────────────────────────────────────────────────────────
void loop() {
  char key = keypad.getKey();
  if (!key) return;

  if (key >= '1' && key <= ('0' + IC_COUNT)) {
    selectedIC = key - '1';
    showSelected();

  } else if (key == '#') {
    if (selectedIC < 0) {
      oledDebugScreen("Error",
                      "No IC selected!",
                      "Press 1-8 first",
                      nullptr, nullptr, false);
      delay(1500);
      showMenu();
    } else {
      oledClear();
      oledPrint("Testing...");
      oledPrint(icList[selectedIC].name);
      oledShow();
      delay(500);
      icList[selectedIC].runTest();
      delay(300);
      showMenu();
    }

  } else if (key == 'A') {
    if (selectedIC >= 0) {
      setPinModes();
      debugWiringCheck();
      showSelected();
    }

  } else if (key == '*') {
    selectedIC = -1;
    showMenu();
  }
}
