/*
 * 倒计时报警装置 (定时炸弹模拟器)
 * 硬件: Arduino Uno R4 WiFi + SSD1306 OLED + 蜂鸣器 + LEDs + 按键 (无需电位器)
 *
 * 玩法:
 *   1. SETUP 模式: ARM 键 +5s, DEFUSE 键 -5s, 长按 ARM 1秒开始
 *   2. ARMED 模式: 倒计时跑起来, OLED 显示大数字倒计时
 *                  蜂鸣器越接近零点叫得越急 (最后 10s 每秒一声, 最后 3s 急速)
 *   3. PAUSED 模式: 按 DEFUSE 暂停, 再按 ARM 继续
 *   4. EXPLODED:    倒计时归零引爆, 蜂鸣器尖叫, 红灯常亮
 *   5. 按 ARM 键复位回到 SETUP
 *
 * OLED 屏幕布局 (128x64):
 *   SETUP:     "SET TIME" + 设定值 + 按键提示
 *   ARMED:     大号倒计时 MM:SS + 进度条 + 状态
 *   EXPLODED:  "!!!  BOOM  !!!" + 闪烁警告
 *
 * 需要安装的库 (Arduino IDE → 工具 → 管理库):
 *   1. Adafruit SSD1306
 *   2. Adafruit GFX Library
 */

// ===== 引入库 =====
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== 引脚定义 =====
#define ARM_BTN     2     // ARM 按键: 一端接 D2, 另一端接 GND (用 INPUT_PULLUP, 按下=0)
#define DEFUSE_BTN  3     // DEFUSE 按键: 一端接 D3, 另一端接 GND
#define BUZZER_PIN  8     // 有源蜂鸣器: 正极接 D8, 负极接 GND
#define RED_LED     9     // 红色 LED: 正极 → 220Ω → D9, 负极 → GND
#define GREEN_LED   10    // 绿色 LED: 正极 → 220Ω → D10, 负极 → GND

// ===== 时间参数 =====
#define TIME_MIN     10   // 最短倒计时 (秒)
#define TIME_MAX     300  // 最长倒计时 (秒) = 5 分钟
#define TIME_STEP    5    // SETUP 模式每次按键加减的秒数
#define BLINK_MED    500  // 中等闪烁间隔 (ms), 倒计时中
#define BLINK_SLOW   1000 // 慢速闪烁间隔 (ms), 安全状态

// ===== OLED 参数 =====
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDR     0x3C

// ===== 状态机枚举 =====
enum BombState {
  STATE_SETUP,     // 设定时间, 绿灯亮
  STATE_ARMED,     // 倒计时运行中, 红灯闪烁
  STATE_PAUSED,    // 倒计时暂停 (拆弹), 绿灯闪烁
  STATE_EXPLODED   // 倒计时归零, 蜂鸣器狂叫
};

// ===== 全局变量 =====
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

BombState state = STATE_SETUP;   // 当前状态

int setTimeSec = 60;               // 设定时间 (秒), 默认 60s, 按键调整
unsigned long remainingMs = 0;   // 剩余时间, 单位毫秒 (ARMED 模式下递减)
unsigned long armedStartMs = 0;  // ARM 时刻的 millis() 记录

unsigned long lastBuzzerToggle = 0;   // 蜂鸣器上次切换时间
unsigned long lastLedToggle = 0;      // LED 上次闪烁时间
unsigned long lastButtonCheck = 0;    // 按键上次检测时间
unsigned long lastDisplayUpdate = 0;  // 屏幕上次刷新时间
unsigned long lastBeep = 0;           // 倒计时中蜂鸣器上次发声时间

bool buzzerState = false;       // 蜂鸣器当前电平
bool ledState = false;          // LED 当前闪烁状态
bool armPrev = true;            // ARM 按键上一帧状态 (上拉, 未按=1)
bool defusePrev = true;         // DEFUSE 按键上一帧状态
unsigned long armPressTime = 0; // ARM 按键按下的时刻 (用于长按检测)

// ===== setup() =====
void setup() {
  Serial.begin(115200);

  // --- OLED 初始化 ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED init FAILED!");
    while (true);
  }

  // --- 引脚初始化 ---
  pinMode(ARM_BTN, INPUT_PULLUP);     // 上拉, 按下=0
  pinMode(DEFUSE_BTN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  // --- 初始状态: 蜂鸣器不响, 红灯灭, 绿灯亮 (SETUP) ---
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);

  // --- 开机画面 ---
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 16);
  display.println(F("BOMB TIMER"));
  display.setCursor(10, 36);
  display.println(F("Countdown Device"));
  display.display();
  delay(1500);

  Serial.println("BOMB TIMER READY");
}

// ===== loop() =====
void loop() {

  // ---------- 第 1 步: 每 20ms 读一次按键 (50Hz) ----------
  unsigned long now = millis();
  if (now - lastButtonCheck >= 20) {
    lastButtonCheck = now;
    checkButtons();
  }

  // ---------- 第 2 步: 更新倒计时时间 ----------
  if (state == STATE_ARMED) {
    updateCountdown();
  }

  // ---------- 第 3 步: 每 100ms 刷一次 OLED ----------
  if (now - lastDisplayUpdate >= 100) {
    lastDisplayUpdate = now;
    updateDisplay();
  }

  // ---------- 第 4 步: 更新蜂鸣器 (非阻塞) ----------
  updateBuzzer();

  // ---------- 第 5 步: 更新 LED (非阻塞) ----------
  updateLEDs();
}

// ============================================================
//  按键检测 (含消抖 + 长按检测)
// ============================================================
void checkButtons() {
  bool armNow = digitalRead(ARM_BTN);
  bool defuseNow = digitalRead(DEFUSE_BTN);

  // --- ARM 键 ---
  if (armPrev == HIGH && armNow == LOW) {
    // 下降沿: ARM 按下
    armPressTime = millis();  // 记录按下时刻, 用于长按检测
  }
  if (armPrev == LOW && armNow == HIGH) {
    // 上升沿: ARM 松开
    unsigned long holdDuration = millis() - armPressTime;
    if (holdDuration >= 1000) {
      // 长按 (≥1 秒)
      handleArmLongPress();
    } else {
      // 短按
      handleArmShortPress();
    }
  }

  // --- DEFUSE 键: 下降沿触发 ---
  if (defusePrev == HIGH && defuseNow == LOW) {
    handleDefusePress();
  }

  armPrev = armNow;
  defusePrev = defuseNow;
}

// -------------------------------------------------------
// ARM 短按: 不同状态功能不同
// -------------------------------------------------------
void handleArmShortPress() {
  switch (state) {

    case STATE_SETUP:
      // 短按 ARM: 时间 +5 秒
      setTimeSec += TIME_STEP;
      if (setTimeSec > TIME_MAX) setTimeSec = TIME_MAX;
      Serial.print("SET: ");
      Serial.print(setTimeSec);
      Serial.println("s");
      break;

    case STATE_ARMED:
      // 倒计时中短按 ARM: 无作用
      break;

    case STATE_PAUSED:
      // 暂停中按 ARM: 恢复倒计时
      armedStartMs = millis();
      state = STATE_ARMED;
      Serial.println("RESUMED!");
      break;

    case STATE_EXPLODED:
      // 爆炸后按 ARM: 复位
      resetToSetup();
      break;
  }
}

// -------------------------------------------------------
// ARM 长按 (≥1 秒): 仅 SETUP 模式有效, 启动倒计时
// -------------------------------------------------------
void handleArmLongPress() {
  if (state == STATE_SETUP) {
    // 确认当前设定时间, 启动倒计时
    remainingMs = (unsigned long)setTimeSec * 1000;
    armedStartMs = millis();
    state = STATE_ARMED;
    Serial.print("ARMED (long press)! Time: ");
    Serial.print(setTimeSec);
    Serial.println("s");
  }
  // 其他状态下长按 ARM 无额外作用
}

// -------------------------------------------------------
// DEFUSE 键: 拆弹/暂停
// -------------------------------------------------------
void handleDefusePress() {
  switch (state) {

    case STATE_SETUP:
      // 短按 DEFUSE: 时间 -5 秒
      setTimeSec -= TIME_STEP;
      if (setTimeSec < TIME_MIN) setTimeSec = TIME_MIN;
      Serial.print("SET: ");
      Serial.print(setTimeSec);
      Serial.println("s");
      break;

    case STATE_ARMED:
      // 倒计时中按 DEFUSE: 暂停!
      state = STATE_PAUSED;
      {
        unsigned long elapsed = millis() - armedStartMs;
        if (elapsed >= remainingMs) {
          remainingMs = 0;
        } else {
          remainingMs -= elapsed;
        }
      }
      Serial.print("PAUSED! Remaining: ");
      Serial.print(remainingMs / 1000);
      Serial.println("s");
      break;

    case STATE_PAUSED:
      // 暂停中按 DEFUSE: 无效
      break;

    case STATE_EXPLODED:
      // 爆炸后按 DEFUSE: 复位
      resetToSetup();
      break;
  }
}

// ============================================================
//  更新倒计时 (仅 ARM 状态调用)
// ============================================================
void updateCountdown() {
  unsigned long now = millis();
  unsigned long elapsed = now - armedStartMs;

  if (elapsed >= remainingMs) {
    // 时间到! 触发爆炸
    remainingMs = 0;
    state = STATE_EXPLODED;
    Serial.println("!!! BOOM !!!");
  }
}

// ============================================================
//  OLED 屏幕绘制
// ============================================================
void updateDisplay() {
  display.clearDisplay();

  switch (state) {
    case STATE_SETUP:
      drawSetupScreen();
      break;
    case STATE_ARMED:
    case STATE_PAUSED:
      drawArmedScreen();
      break;
    case STATE_EXPLODED:
      drawExplodedScreen();
      break;
  }

  display.display();
}

// --- SETUP 画面: 显示设定时间 + 按键操作提示 ---
void drawSetupScreen() {
  int min = setTimeSec / 60;
  int sec = setTimeSec % 60;

  // 标题
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("---- SET TIME ----"));

  // 大号时间
  display.setTextSize(3);  // 18x24 像素/字符
  display.setCursor(10, 16);
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", min, sec);
  display.print(timeStr);

  // 操作提示
  display.setTextSize(1);
  display.setCursor(0, 50);
  display.println(F("D:-5s  A:+5s  (hold A=start)"));
}

// --- ARMED / PAUSED 画面: 大号倒计时 + 进度条 ---
void drawArmedScreen() {
  // 计算当前剩余时间
  unsigned long remain;
  unsigned long total;

  if (state == STATE_ARMED) {
    unsigned long elapsed = millis() - armedStartMs;
    remain = (elapsed >= remainingMs) ? 0 : (remainingMs - elapsed);
    total = (unsigned long)setTimeSec * 1000;
  } else {  // PAUSED
    remain = remainingMs;
    total = (unsigned long)setTimeSec * 1000;
  }

  int remainSec = remain / 1000;
  int min = remainSec / 60;
  int sec = remainSec % 60;

  // 状态标签
  display.setTextSize(1);
  display.setCursor(0, 0);
  if (state == STATE_ARMED) {
    display.println(F("** ARMED **"));
  } else {
    display.println(F("** PAUSED (defuse) **"));
  }

  // 大号倒计时数字
  display.setTextSize(3);
  display.setCursor(10, 14);
  char timeStr[6];
  sprintf(timeStr, "%02d:%02d", min, sec);
  display.print(timeStr);

  // 进度条 (y=48)
  display.setTextSize(1);
  int barY = 52;
  display.drawRect(0, barY, 128, 8, SSD1306_WHITE);
  if (total > 0) {
    int progressPct = (int)(remain * 100 / total);
    int barWidth = map(progressPct, 0, 100, 0, 126);
    display.fillRect(1, barY + 1, barWidth, 6, SSD1306_WHITE);
  }

  // 剩余秒数, 显示在进度条上方
  display.setCursor(0, barY - 8);
  display.print(F("Remain: "));
  display.print(remainSec);
  display.print("s");
}

// --- EXPLODED 画面: 爆炸警告 ---
void drawExplodedScreen() {
  // 让画面交替闪烁 (利用 ledState 变量)
  display.setTextSize(2);
  display.setCursor(8, 10);
  display.setTextColor(SSD1306_WHITE);
  display.println(F("!!! BOOM !!!"));

  display.setTextSize(1);
  display.setCursor(10, 36);
  display.println(F("TIME'S UP!"));

  display.setCursor(8, 52);
  display.println(F("ARM to reset"));
}

// ============================================================
//  蜂鸣器控制 (非阻塞, 不卡主循环)
// ============================================================
void updateBuzzer() {
  unsigned long now = millis();
  int beepInterval;  // 蜂鸣器切换间隔

  switch (state) {

    case STATE_SETUP:
      // SETUP 模式不响
      digitalWrite(BUZZER_PIN, LOW);
      break;

    case STATE_ARMED: {
      // 计算当前剩余秒数
      unsigned long elapsed = millis() - armedStartMs;
      unsigned long remain = (elapsed >= remainingMs) ? 0 : (remainingMs - elapsed);
      int remainSec = remain / 1000;

      // 根据剩余时间决定蜂鸣器频率
      if (remainSec <= 3) {
        beepInterval = 150;   // 最后 3 秒: 急速滴滴滴
      } else if (remainSec <= 10) {
        beepInterval = 400;   // 最后 10 秒: 每秒一声
      } else {
        beepInterval = 2000;  // 10 秒以上: 每 2 秒一声滴
      }

      if (now - lastBuzzerToggle >= beepInterval) {
        lastBuzzerToggle = now;
        buzzerState = !buzzerState;
        digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      }
      break;
    }

    case STATE_PAUSED:
      // 暂停: 静音
      digitalWrite(BUZZER_PIN, LOW);
      break;

    case STATE_EXPLODED:
      // 爆炸: 急速交替尖叫
      beepInterval = 100;  // 100ms 一个周期, 高频尖叫
      if (now - lastBuzzerToggle >= beepInterval) {
        lastBuzzerToggle = now;
        buzzerState = !buzzerState;
        digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      }
      break;
  }
}

// ============================================================
//  LED 状态指示 (非阻塞)
// ============================================================
void updateLEDs() {
  unsigned long now = millis();
  int ledInterval;

  switch (state) {

    case STATE_SETUP:
      // SETUP: 绿灯常亮, 红灯灭 (安全)
      digitalWrite(GREEN_LED, HIGH);
      digitalWrite(RED_LED, LOW);
      break;

    case STATE_ARMED: {
      // ARMED: 红灯闪烁, 频率随剩余时间加快
      unsigned long elapsed = millis() - armedStartMs;
      unsigned long remain = (elapsed >= remainingMs) ? 0 : (remainingMs - elapsed);
      int remainSec = remain / 1000;

      if (remainSec <= 5) {
        ledInterval = 150;   // 最后 5 秒: 狂闪
      } else if (remainSec <= 15) {
        ledInterval = 300;   // 15 秒内: 快速闪
      } else {
        ledInterval = 500;   // 正常: 中等速度闪
      }

      digitalWrite(GREEN_LED, LOW);
      if (now - lastLedToggle >= ledInterval) {
        lastLedToggle = now;
        ledState = !ledState;
        digitalWrite(RED_LED, ledState ? HIGH : LOW);
      }
      break;
    }

    case STATE_PAUSED:
      // PAUSED: 绿灯闪烁 (表示已拆弹但未复位)
      digitalWrite(RED_LED, LOW);
      if (now - lastLedToggle >= BLINK_MED) {
        lastLedToggle = now;
        ledState = !ledState;
        digitalWrite(GREEN_LED, ledState ? HIGH : LOW);
      }
      break;

    case STATE_EXPLODED:
      // EXPLODED: 红灯常亮 / 极速闪烁
      digitalWrite(GREEN_LED, LOW);
      if (now - lastLedToggle >= 80) {  // 80ms 极快闪
        lastLedToggle = now;
        ledState = !ledState;
        digitalWrite(RED_LED, ledState ? HIGH : LOW);
      }
      break;
  }
}

// ============================================================
//  复位: 回到 SETUP 模式
// ============================================================
void resetToSetup() {
  state = STATE_SETUP;
  remainingMs = 0;
  // setTimeSec 保持用户上次设定的值不变, 方便快速重来
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  Serial.println("RESET to SETUP");
}
