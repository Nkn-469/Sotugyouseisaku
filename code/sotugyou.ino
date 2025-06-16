// 必要なライブラリをインクルード
#include <Adafruit_GFX.h>             // OLED用グラフィックライブラリ
#include <Adafruit_SSD1306.h>         // SSD1306 OLED用ライブラリ
#include <Wire.h>                     // I2C通信ライブラリ
#include <Adafruit_NeoPixel.h>        // WS2812用ライブラリ
#include "Adafruit_Keypad.h"          // Adafruit製キーパッドライブラリ

// ピン設定
const int trigPin = 3;               // 超音波センサーのTrigピン
const int echoPin = 2;               // 超音波センサーのEchoピン
const int thermistorPin = A0;        // サーミスタの接続ピン
const int motorIn1 = 7;              // モーター制御ピン1
const int motorIn2 = 6;              // モーター制御ピン2
const int objectLedPin = 5;          // 単色LED制御ピン
#define LED_PIN     4                // NeoPixel LED制御ピン
#define LED_COUNT   8                // NeoPixel LEDの数

// OLEDディスプレイ設定
#define SCREEN_WIDTH 128             // OLEDの幅
#define SCREEN_HEIGHT 64             // OLEDの高さ
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);  // OLEDインスタンス作成

// NeoPixel LED設定
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800); // NeoPixelインスタンス作成

// キーパッド設定
const byte ROWS = 4;  // 行数
const byte COLS = 4;  // 列数
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {8, 9, 10, 11};         // キーパッドの行ピン
byte colPins[COLS] = {A1, A2, A3, 12};       // キーパッドの列ピン

// Adafruit_Keypadのインスタンス作成
Adafruit_Keypad keypad = Adafruit_Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// 動作設定
const float tempThreshold = 20.0;            // 温度しきい値（℃）
const int objectDistanceThreshold = 20;      // 距離しきい値（cm）
const byte MOTOR_PWM_MIN = 60;               // モーター最低PWM
bool autoMode = true;                        // 自動モード（初期値：true）
byte manualWindLevel = 4;                    // 手動モード時の風力レベル（初期値：4）

// 初期化
void setup() {
  Serial.begin(9600);                        // シリアル通信初期化
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  pinMode(motorIn1, OUTPUT);
  pinMode(motorIn2, OUTPUT);
  pinMode(objectLedPin, OUTPUT);

  // OLEDの初期化
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
    while (1);  // OLEDが見つからない場合は停止
  }

  display.clearDisplay();
  display.display();

  strip.begin();  // NeoPixel LED初期化
  strip.show();   // 全消灯

  keypad.begin(); // Adafruitキーパッド初期化
}

// 超音波センサーによる距離の取得
long getDistance() {
  digitalWrite(trigPin, LOW); // クリーンなパルスを送るためにLOWに設定
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH); // TrigピンをHIGHにして超音波を発信
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);  // トリガーピンをLOWに戻す
  long duration = pulseIn(echoPin, HIGH, 30000);  // 最大30msまで待機
  if (duration == 0) return 999;  // タイムアウト時は999cm扱い
  return duration * 0.034 / 2;    // 距離の計算
}

// 温度の取得（摂氏）
float getTemperature() {
  int analogValue = analogRead(thermistorPin);
  const int beta = 3950;                   // サーミスタのB定数
  const float resistance = 10000.0;        // 10kΩ
  float r = (1023.0 / analogValue - 1.0) * resistance;  // サーミスタの抵抗値の計算
  float tempK = 1.0 / (log(r / resistance) / beta + 1.0 / 298.15); // 温度の計算（ボルツマン定数）

  return tempK - 273.15;                   // ケルビン → 摂氏
}

// 温度から風力レベル（0〜8）
byte calculateWindLevel(float temp) {
  const float tempMin = 20.0; // 最低温度（20度以下は風力0）
  const float tempMax = 36.0; // 最高温度（36度以上は風力8）
  int level = map(temp * 10, (int)(tempMin * 10), (int)(tempMax * 10), 0, 8);
  return constrain(level, 0, 8);           // 0〜8に制限
}

// モーター回転速度を設定
void setMotorSpeed(byte level, long dist) {
  if (level == 0) {
    analogWrite(motorIn2, 0);  // 停止
  } else {
    if (autoMode && dist > objectDistanceThreshold) {
      analogWrite(motorIn2, 0);  // 自動モードでは距離判定
    } else {
      byte pwm = map(level, 1, 8, MOTOR_PWM_MIN, 255);
      analogWrite(motorIn2, pwm);
    }
  }
  digitalWrite(motorIn1, LOW); // モーターの逆回転を防ぐため常にLOW
}

// モーターが回せるかどうか判定
bool canMotorRun(byte level, long dist) {
  return (level >= 1 && dist <= objectDistanceThreshold);
}

// 風力レベルに応じた色（青→緑→赤）
uint32_t colorGradient(byte level) {
  if (level == 0) return strip.Color(0, 0, 0);  // 消灯

  if (level >= 1 && level <= 4) {
    // 青(0,0,255) → 緑(0,255,0)
    float ratio = (level - 1) / 3.0;
    byte r = 0;
    byte g = 255 * ratio;
    byte b = 255 * (1.0 - ratio);
    return strip.Color(r, g, b);
  } else if (level >= 5 && level <= 8) {
    // 緑(0,255,0) → 赤(255,0,0)
    float ratio = (level - 4) / 4.0;
    byte r = 255 * ratio;
    byte g = 255 * (1.0 - ratio);
    byte b = 0;
    return strip.Color(r, g, b);
  }

  return strip.Color(0, 0, 0); // 念のため
}

// NeoPixel LEDを更新
void updateWindLED(byte level, long dist) {
  strip.clear();  // すべてのLEDを消灯

  if (!autoMode) {
    for (byte i = 0; i < level; i++) {
      strip.setPixelColor(i, colorGradient(i + 1));
    }
    strip.show();
    return;
  }

  if (!canMotorRun(level, dist)) {
    strip.show();
    return;
  }

  for (byte i = 0; i < level; i++) {
    strip.setPixelColor(i, colorGradient(i + 1)); // 風力レベルに応じた色
  }
  strip.show();  // LEDを更新
}

// 単色LEDの明るさ更新
void updateObjectLED(float temp, long dist) {
  bool tempHigh = temp > tempThreshold;            // 温度がしきい値を超えているか
  bool objectNear = dist <= objectDistanceThreshold; // 物体が近いか
  if (tempHigh && objectNear) {
    analogWrite(objectLedPin, 255);  // 高温かつ物体が近ければ明るさ最大
  } else if (tempHigh || objectNear) {
    analogWrite(objectLedPin, 128);  // 温度が高いまたは物体が近い場合は明るさ半分
  } else {
    analogWrite(objectLedPin, 0);    // それ以外は消灯
  }
}

// OLED表示を更新
void updateDisplay(float temp, long dist, byte windLevel) {
  display.clearDisplay();  // 画面をクリア
  display.setTextSize(1);  // フォントサイズを設定
  display.setTextColor(SSD1306_WHITE);  // 文字色を白に設定
  display.setCursor(0, 0); // 画面の左上にカーソルを移動

  // 温度、距離、風力レベル、モードを表示
  display.print("Temp: ");
  display.print(temp, 1);
  display.println(" C");

  display.print("Dist: ");
  display.print(dist);
  display.println(" cm");

  display.print("Wind: ");
  display.println(windLevel);

  display.print("Mode: ");
  display.println(autoMode ? "Auto" : "Manual");

  display.display();  // 画面を更新
}

// メインループ
void loop() {
  keypad.tick();  // キーパッドの状態更新

  // キーパッドのイベントがあれば取得
  while (keypad.available()) {
    keypadEvent evt = keypad.read();  // ← ここを修正（keypad_event → keypadEvent）
    if (evt.bit.EVENT == KEY_JUST_PRESSED) {
      char key = evt.bit.KEY;
      if (key == 'A') {
        autoMode = !autoMode;  // Aキーで自動・手動切り替え
      } else if (!autoMode) {
        if (key >= '1' && key <= '8') {
          manualWindLevel = key - '0';  // 手動風力設定
        } else if (key == '0') {
          manualWindLevel = 0;          // 手動風力停止
        }
      }
    }
  }

  float temp = getTemperature();                  // 温度取得
  long dist = getDistance();                      // 距離取得

  byte windLevel = autoMode ? calculateWindLevel(temp) : manualWindLevel;  // 風力決定

  updateObjectLED(temp, dist);                    // 単色LED更新
  updateWindLED(windLevel, dist);                 // NeoPixel更新
  setMotorSpeed(windLevel, dist);                 // モーター制御
  updateDisplay(temp, dist, windLevel);           // OLED表示更新

  delay(200);  // 更新間隔
}
