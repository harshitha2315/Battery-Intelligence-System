
#define BLYNK_TEMPLATE_ID "TMPL327owI03W"
#define BLYNK_TEMPLATE_NAME "Battery Intelligence System"
#define BLYNK_AUTH_TOKEN "Ar-L7UpFThmYmTjXbqgeoBCauNbpdvL9"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

#define CELL_COUNT 4

#define CELL1_PIN 1
#define CELL2_PIN 2
#define CELL3_PIN 3
#define CELL4_PIN 4

#define RELAY_PIN 5
#define BUZZER_PIN 6
#define RELAY_STATUS_LED 7

#define LCD_SDA 8
#define LCD_SCL 9
#define RELAY_FEEDBACK_PIN 10

LiquidCrystal_I2C lcd(0x27, 16, 2);

char ssid[] = "Wokwi-GUEST";
char pass[] = "";

const int cellPins[CELL_COUNT] = {CELL1_PIN, CELL2_PIN, CELL3_PIN, CELL4_PIN};

unsigned long previousBatteryReadTime = 0;
unsigned long previousPrintTime = 0;
unsigned long previousLCDTime = 0;
unsigned long previousBlynkCheckTime = 0;

const unsigned long batteryReadInterval = 500;
const unsigned long printInterval = 1000;
const unsigned long lcdInterval = 2000;
const unsigned long blynkCheckInterval = 3000;

const unsigned long recoveryDelayTime = 5000;
const unsigned long relayFeedbackGraceTime = 2000;
const unsigned long relayMismatchConfirmTime = 2000;

const float rapidVoltageChangeLimit = 0.50;
const float adcChangeThreshold = 0.05;
const unsigned long frozenADCTimeout = 10000;

float lastStableVoltage[CELL_COUNT];
unsigned long frozenStartTime[CELL_COUNT];

unsigned long safeConditionStartTime = 0;
unsigned long relayTurnOnTime = 0;
unsigned long relayMismatchStartTime = 0;

bool faultActive = false;
int lcdScreen = 0;
int frozenCellNumber = 0;

String lastSentHealth = "";
String lastSentMode = "";
String lastSentFault = "";
float lastSentPackVoltage = -1;
int lastSentRSSI = 0;

bool queuedTelemetry = false;

String lastSentRecommendation = "";
String lastSentFaultHistory = "";
String lastSentRiskLevel = "";
int lastSOC = -1;

enum RuntimeMode {
  NORMAL,
  DEGRADED,
  FAILSAFE,
  SHUTDOWN
};

RuntimeMode currentMode = NORMAL;
String runtimeModeText = "NORMAL";

struct BatteryData {
  float cellVoltage[CELL_COUNT];
  float previousCellVoltage[CELL_COUNT];
  float packVoltage;
  float averageVoltage;
  float strongestVoltage;
  int strongestCell;
  float weakestVoltage;
  int weakestCell;
  float imbalancePercent;
  String healthStatus;
};

struct ProtectionData {
  bool relayState;
  bool buzzerState;
  bool weakCellFault;
  bool overVoltageFault;
  bool sensorFault;
  bool criticalImbalanceFault;
  bool rapidVoltageFault;
  bool invalidReadingFault;
  bool frozenADCFault;
  bool relayMismatchFault;
  String protectionStatus;
  String lastFaultLog;
};

BatteryData battery;
ProtectionData protection;

String getRiskLevel() {

  if (currentMode == SHUTDOWN)
    return "CRITICAL";

  if (currentMode == FAILSAFE)
    return "HIGH";

  if (currentMode == DEGRADED)
    return "MEDIUM";

  return "LOW";
}

String getOperatorRecommendation() {

  if (protection.sensorFault)
    return "Inspect battery sensor immediately.";

  if (protection.weakCellFault)
    return "Replace weak battery cell.";

  if (protection.overVoltageFault)
    return "Disconnect charger.";

  if (protection.criticalImbalanceFault)
    return "Balance battery pack.";

  if (protection.rapidVoltageFault)
    return "Monitor sudden voltage fluctuation.";

  if (protection.relayMismatchFault)
    return "Inspect relay circuit.";

  if (protection.invalidReadingFault)
    return "Verify ADC input.";

  if (protection.frozenADCFault)
    return "Restart ADC subsystem.";

  return "Battery operating normally.";
}

String getFaultHistory() {
  return protection.lastFaultLog;
}
int calculateSOC() {

  float soc = ((battery.averageVoltage - 2.5) / (3.3 - 2.5)) * 100.0;

  if (soc < 0) soc = 0;
  if (soc > 100) soc = 100;

  return (int)soc;
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RELAY_STATUS_LED, OUTPUT);
  pinMode(RELAY_FEEDBACK_PIN, INPUT);

  digitalWrite(RELAY_PIN, HIGH);
  digitalWrite(RELAY_STATUS_LED, HIGH);
  noTone(BUZZER_PIN);

  protection.relayState = true;
  protection.buzzerState = false;
  protection.protectionStatus = "NORMAL";
  protection.lastFaultLog = "No faults recorded";
  relayTurnOnTime = millis();

  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Smart BMS");
  lcd.setCursor(0, 1);
  lcd.print("Cloud Start");

  for (int i = 0; i < CELL_COUNT; i++) {
    battery.previousCellVoltage[i] = 0;
    lastStableVoltage[i] = 0;
    frozenStartTime[i] = 0;
  }

WiFi.mode(WIFI_STA);
Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "blynk.cloud", 80);

  Serial.println("Task 5 - Intelligent Cloud Telemetry Started");
}

void loop() {
  unsigned long currentTime = millis();

  if (WiFi.status() == WL_CONNECTED) {
    Blynk.run();
  }

  if (currentTime - previousBatteryReadTime >= batteryReadInterval) {
    previousBatteryReadTime = currentTime;

    updatePreviousVoltages();
    readCellVoltages();
    calculatePackVoltage();
    calculateAverageVoltage();
    findStrongestAndWeakestCell();
    calculateImbalance();
    classifyBatteryHealth();

    detectFaults(currentTime);
    updateRuntimeMode();
    updateProtectionSystem(currentTime);
  }

  if (currentTime - previousPrintTime >= printInterval) {
    previousPrintTime = currentTime;
    printSystemReport();
  }

  if (currentTime - previousLCDTime >= lcdInterval) {
    previousLCDTime = currentTime;
    updateLCDDisplay();
  }

  if (currentTime - previousBlynkCheckTime >= blynkCheckInterval) {
    previousBlynkCheckTime = currentTime;
    handleCloudTelemetry();
  }
}

float convertAdcToVoltage(int adcValue) {
  return adcValue * 3.3 / 4095.0;
}

void updatePreviousVoltages() {
  for (int i = 0; i < CELL_COUNT; i++) {
    battery.previousCellVoltage[i] = battery.cellVoltage[i];
  }
}

void readCellVoltages() {
  for (int i = 0; i < CELL_COUNT; i++) {
    int adcValue = analogRead(cellPins[i]);
    battery.cellVoltage[i] = convertAdcToVoltage(adcValue);
  }
}

void calculatePackVoltage() {
  battery.packVoltage = 0;
  for (int i = 0; i < CELL_COUNT; i++) {
    battery.packVoltage += battery.cellVoltage[i];
  }
}

void calculateAverageVoltage() {
  battery.averageVoltage = battery.packVoltage / CELL_COUNT;
}

void findStrongestAndWeakestCell() {
  battery.strongestVoltage = battery.cellVoltage[0];
  battery.weakestVoltage = battery.cellVoltage[0];
  battery.strongestCell = 1;
  battery.weakestCell = 1;

  for (int i = 1; i < CELL_COUNT; i++) {
    if (battery.cellVoltage[i] > battery.strongestVoltage) {
      battery.strongestVoltage = battery.cellVoltage[i];
      battery.strongestCell = i + 1;
    }

    if (battery.cellVoltage[i] < battery.weakestVoltage) {
      battery.weakestVoltage = battery.cellVoltage[i];
      battery.weakestCell = i + 1;
    }
  }
}

void calculateImbalance() {
  if (battery.averageVoltage > 0) {
    battery.imbalancePercent =
      ((battery.strongestVoltage - battery.weakestVoltage) /
       battery.averageVoltage) * 100.0;
  } else {
    battery.imbalancePercent = 0;
  }
}

void classifyBatteryHealth() {
  bool packFailure = false;

  for (int i = 0; i < CELL_COUNT; i++) {
    if (battery.cellVoltage[i] < 2.5) {
      packFailure = true;
    }
  }

  if (packFailure) {
    battery.healthStatus = "PACK FAILURE";
  }
  else if (battery.imbalancePercent < 3) {
    battery.healthStatus = "HEALTHY";
  }
  else if (battery.imbalancePercent < 10) {
    battery.healthStatus = "MINOR IMBAL";
  }
  else {
    battery.healthStatus = "CRITICAL";
  }
}

void detectFaults(unsigned long currentTime) {
  protection.weakCellFault = false;
  protection.overVoltageFault = false;
  protection.sensorFault = false;
  protection.criticalImbalanceFault = false;
  protection.rapidVoltageFault = false;
  protection.invalidReadingFault = false;
  protection.frozenADCFault = false;
  protection.relayMismatchFault = false;
  frozenCellNumber = 0;

  for (int i = 0; i < CELL_COUNT; i++) {
    if (battery.cellVoltage[i] < 2.5) protection.weakCellFault = true;
    if (battery.cellVoltage[i] > 4.25) protection.overVoltageFault = true;
    if (battery.cellVoltage[i] < 0.05) protection.sensorFault = true;

    float voltageDifference = abs(battery.cellVoltage[i] - battery.previousCellVoltage[i]);

    if (battery.previousCellVoltage[i] > 0 && voltageDifference > rapidVoltageChangeLimit) {
      protection.rapidVoltageFault = true;
    }

    if (battery.cellVoltage[i] < 0.0 || battery.cellVoltage[i] > 3.31) {
      protection.invalidReadingFault = true;
    }

    bool activeRange = battery.cellVoltage[i] > 0.10 && battery.cellVoltage[i] < 3.20;

    if (activeRange) {
      float stableDifference = abs(battery.cellVoltage[i] - lastStableVoltage[i]);

      if (stableDifference > adcChangeThreshold) {
        lastStableVoltage[i] = battery.cellVoltage[i];
        frozenStartTime[i] = currentTime;
      }
      else {
        if (frozenStartTime[i] == 0) frozenStartTime[i] = currentTime;

        if (currentTime - frozenStartTime[i] >= frozenADCTimeout) {
          protection.frozenADCFault = true;
          frozenCellNumber = i + 1;
        }
      }
    }
    else {
      lastStableVoltage[i] = battery.cellVoltage[i];
      frozenStartTime[i] = currentTime;
    }
  }

  if (battery.healthStatus == "CRITICAL") {
    protection.criticalImbalanceFault = true;
  }

  checkRelayMismatch(currentTime);

  faultActive =
    protection.sensorFault ||
    protection.weakCellFault ||
    protection.relayMismatchFault ||
    protection.invalidReadingFault ||
    protection.frozenADCFault ||
    protection.overVoltageFault ||
    protection.criticalImbalanceFault ||
    protection.rapidVoltageFault;

  updateFaultLog(currentTime);
}

void checkRelayMismatch(unsigned long currentTime) {
  bool relayFeedback = digitalRead(RELAY_FEEDBACK_PIN);

  if (protection.relayState == true) {
    if (currentTime - relayTurnOnTime >= relayFeedbackGraceTime) {
      if (relayFeedback == LOW) {
        if (relayMismatchStartTime == 0) relayMismatchStartTime = currentTime;

        if (currentTime - relayMismatchStartTime >= relayMismatchConfirmTime) {
          protection.relayMismatchFault = true;
        }
      }
      else {
        relayMismatchStartTime = 0;
      }
    }
  }
  else {
    relayMismatchStartTime = 0;
  }
}

void updateRuntimeMode() {
  if (protection.sensorFault ||
      protection.weakCellFault ||
      protection.relayMismatchFault ||
      protection.invalidReadingFault ||
      protection.frozenADCFault) {
    currentMode = SHUTDOWN;
    runtimeModeText = "SHUTDOWN";
  }
  else if (protection.criticalImbalanceFault ||
           protection.rapidVoltageFault ||
           protection.overVoltageFault) {
    currentMode = FAILSAFE;
    runtimeModeText = "FAILSAFE";
  }
  else if (battery.healthStatus == "MINOR IMBAL") {
    currentMode = DEGRADED;
    runtimeModeText = "DEGRADED";
  }
  else {
    currentMode = NORMAL;
    runtimeModeText = "NORMAL";
  }
}

void updateFaultLog(unsigned long currentTime) {
  if (protection.sensorFault) protection.lastFaultLog = "[" + String(currentTime) + " ms] SENSOR FAULT";
  else if (protection.weakCellFault) protection.lastFaultLog = "[" + String(currentTime) + " ms] WEAK CELL";
  else if (protection.relayMismatchFault) protection.lastFaultLog = "[" + String(currentTime) + " ms] RELAY MISMATCH";
  else if (protection.invalidReadingFault) protection.lastFaultLog = "[" + String(currentTime) + " ms] INVALID READING";
  else if (protection.frozenADCFault) protection.lastFaultLog = "[" + String(currentTime) + " ms] FROZEN ADC CELL " + String(frozenCellNumber);
  else if (protection.overVoltageFault) protection.lastFaultLog = "[" + String(currentTime) + " ms] OVERVOLTAGE";
  else if (protection.criticalImbalanceFault) protection.lastFaultLog = "[" + String(currentTime) + " ms] CRITICAL IMBAL";
  else if (protection.rapidVoltageFault) protection.lastFaultLog = "[" + String(currentTime) + " ms] RAPID CHANGE";
}

void updateProtectionSystem(unsigned long currentTime) {
  if (faultActive) {
    safeConditionStartTime = 0;

    if (protection.relayState == true) {
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(RELAY_STATUS_LED, LOW);
      protection.relayState = false;
    }

    protection.buzzerState = true;

    if (protection.sensorFault) {
      tone(BUZZER_PIN, 1200);
      protection.protectionStatus = "SENSOR FAULT";
    }
    else if (protection.weakCellFault) {
      tone(BUZZER_PIN, 1000);
      protection.protectionStatus = "WEAK CELL";
    }
    else if (protection.relayMismatchFault) {
      tone(BUZZER_PIN, 2200);
      protection.protectionStatus = "RELAY MISMATCH";
    }
    else if (protection.invalidReadingFault) {
      tone(BUZZER_PIN, 2000);
      protection.protectionStatus = "INVALID READ";
    }
    else if (protection.frozenADCFault) {
      tone(BUZZER_PIN, 700);
      protection.protectionStatus = "ADC FROZEN";
    }
    else if (protection.overVoltageFault) {
      tone(BUZZER_PIN, 1500);
      protection.protectionStatus = "OVERVOLTAGE";
    }
    else if (protection.rapidVoltageFault) {
      tone(BUZZER_PIN, 1800);
      protection.protectionStatus = "RAPID CHANGE";
    }
    else if (protection.criticalImbalanceFault) {
      tone(BUZZER_PIN, 900);
      protection.protectionStatus = "CRIT IMBAL";
    }
  }
  else {
    noTone(BUZZER_PIN);
    protection.buzzerState = false;

    if (safeConditionStartTime == 0) safeConditionStartTime = currentTime;

    if (protection.relayState == false) {
      if (currentTime - safeConditionStartTime >= recoveryDelayTime) {
        digitalWrite(RELAY_PIN, HIGH);
        digitalWrite(RELAY_STATUS_LED, HIGH);

        protection.relayState = true;
        relayTurnOnTime = currentTime;
        relayMismatchStartTime = 0;

        protection.protectionStatus = "RECOVERED";
        protection.lastFaultLog = "[" + String(currentTime) + " ms] SYSTEM RECOVERED";
      }
      else {
        protection.protectionStatus = "RECOVERY WAIT";
      }
    }
    else {
      protection.protectionStatus = "NORMAL";
    }
  }
}

void updateLCDDisplay() {
  lcd.clear();

  if (currentMode == SHUTDOWN) {
    lcd.setCursor(0, 0);
    lcd.print("MODE:SHUTDOWN");
    lcd.setCursor(0, 1);
    lcd.print(protection.protectionStatus);
    return;
  }

  if (currentMode == FAILSAFE) {
    lcd.setCursor(0, 0);
    lcd.print("MODE:FAILSAFE");
    lcd.setCursor(0, 1);
    lcd.print(protection.protectionStatus);
    return;
  }

  if (protection.protectionStatus == "RECOVERY WAIT") {
    lcd.setCursor(0, 0);
    lcd.print("RECOVERY WAIT");
    lcd.setCursor(0, 1);
    lcd.print("Relay OFF Safe");
    return;
  }

  switch (lcdScreen) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("Pack:");
      lcd.print(battery.packVoltage, 2);
      lcd.print("V");
      lcd.setCursor(0, 1);
      lcd.print("Avg:");
      lcd.print(battery.averageVoltage, 2);
      lcd.print("V");
      break;

    case 1:
      lcd.setCursor(0, 0);
      lcd.print("Health:");
      lcd.print(battery.healthStatus);
      lcd.setCursor(0, 1);
      lcd.print("Imb:");
      lcd.print(battery.imbalancePercent, 1);
      lcd.print("%");
      break;

    case 2:
      lcd.setCursor(0, 0);
      lcd.print("Mode:");
      lcd.print(runtimeModeText);
      lcd.setCursor(0, 1);
      lcd.print("Relay:");
      lcd.print(protection.relayState ? "ON " : "OFF");
      break;

    case 3:
      lcd.setCursor(0, 0);
      lcd.print("Strong:C");
      lcd.print(battery.strongestCell);
      lcd.print(" ");
      lcd.print(battery.strongestVoltage, 2);
      lcd.setCursor(0, 1);
      lcd.print("Weak:C");
      lcd.print(battery.weakestCell);
      lcd.print(" ");
      lcd.print(battery.weakestVoltage, 2);
      break;
  }

  lcdScreen++;
  if (lcdScreen > 3) lcdScreen = 0;
}

void handleCloudTelemetry() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected - reconnecting...");
    WiFi.begin(ssid, pass, 6);
    queuedTelemetry = true;
    return;
  }

  if (!Blynk.connected()) {
    Serial.println("Blynk disconnected - reconnecting...");
    Blynk.connect();
    queuedTelemetry = true;
    return;
  }

  int rssi = WiFi.RSSI();
  String currentFault = protection.protectionStatus;
  String recommendation = getOperatorRecommendation();
String faultHistory = getFaultHistory();
String riskLevel = getRiskLevel();
int soc = calculateSOC();
 bool stateChanged =

    abs(battery.packVoltage-lastSentPackVoltage)>0.20 ||

    battery.healthStatus!=lastSentHealth ||

    runtimeModeText!=lastSentMode ||

    currentFault!=lastSentFault ||

    abs(rssi-lastSentRSSI)>5 ||

    recommendation!=lastSentRecommendation ||

    faultHistory!=lastSentFaultHistory ||

    riskLevel!=lastSentRiskLevel ||

    soc != lastSOC ||

    faultActive ||

    queuedTelemetry;

  if (stateChanged) {
    Blynk.virtualWrite(V0, battery.packVoltage);
    Blynk.virtualWrite(V1, battery.healthStatus);
    Blynk.virtualWrite(V2, runtimeModeText);
    Blynk.virtualWrite(V3, currentFault);
    Blynk.virtualWrite(V4, rssi);
    Blynk.virtualWrite(V5,recommendation);
    Blynk.virtualWrite(V6,faultHistory);
    Blynk.virtualWrite(V7,riskLevel);
    Blynk.virtualWrite(V8, soc);
    Blynk.virtualWrite(V9, battery.cellVoltage[0]);
    Blynk.virtualWrite(V10, battery.cellVoltage[1]);
    Blynk.virtualWrite(V11, battery.cellVoltage[2]);
    Blynk.virtualWrite(V12, battery.cellVoltage[3]);

    lastSentPackVoltage = battery.packVoltage;
    lastSentHealth = battery.healthStatus;
    lastSentMode = runtimeModeText;
    lastSentFault = currentFault;
    lastSentRSSI = rssi;
    lastSentRecommendation = recommendation;
    lastSentFaultHistory = faultHistory;
    lastSentRiskLevel = riskLevel;
    lastSOC = soc;
    queuedTelemetry = false;

    Serial.println("Cloud Telemetry Sent to Blynk");
  }
  else {
    Serial.println("No major state change - telemetry skipped");
  }
}

void printSystemReport() {
  Serial.println("-----------");

  for (int i = 0; i < CELL_COUNT; i++) {
    Serial.print("Cell");
    Serial.print(i + 1);
    Serial.print(": ");
    Serial.println(battery.cellVoltage[i], 2);
  }

  Serial.print("Pack Voltage: ");
  Serial.println(battery.packVoltage, 2);
  Serial.print("Average Voltage: ");
  Serial.println(battery.averageVoltage, 2);
  Serial.print("Weakest Cell: Cell ");
  Serial.print(battery.weakestCell);
  Serial.print(" = ");
  Serial.println(battery.weakestVoltage, 2);
  Serial.print("Strongest Cell: Cell ");
  Serial.print(battery.strongestCell);
  Serial.print(" = ");
  Serial.println(battery.strongestVoltage, 2);
  Serial.print("Imbalance: ");
  Serial.print(battery.imbalancePercent, 2);
  Serial.println(" %");
  Serial.print("Health Status: ");
  Serial.println(battery.healthStatus);
  Serial.print("Runtime Mode: ");
  Serial.println(runtimeModeText);
  Serial.print("Fault Active: ");
  Serial.println(faultActive ? "YES" : "NO");
  Serial.print("Protection Status: ");
  Serial.println(protection.protectionStatus);
  Serial.print("Relay State: ");
  Serial.println(protection.relayState ? "ON" : "OFF");
  Serial.print("Buzzer State: ");
  Serial.println(protection.buzzerState ? "ON" : "OFF");
  Serial.print("Relay Feedback: ");
  Serial.println(digitalRead(RELAY_FEEDBACK_PIN) == HIGH ? "HIGH" : "LOW");
  Serial.print("WiFi RSSI: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  Serial.print("Blynk Status: ");
  Serial.println(Blynk.connected() ? "CONNECTED" : "DISCONNECTED");
  Serial.print("Last Fault Log: ");
  Serial.println(protection.lastFaultLog);
}