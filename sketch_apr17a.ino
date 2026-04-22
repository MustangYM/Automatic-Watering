#include <WiFi.h>
#include <WebServer.h>
#include <time.h>

// ==============================
// 配置你的 Wi-Fi
// ==============================
const char* WIFI_SSID = "";
const char* WIFI_PASS = "";

// 北京时间 NTP
const char* TZ_INFO = "CST-8";
const char* NTP_SERVER_1 = "ntp1.aliyun.com";
const char* NTP_SERVER_2 = "ntp.ntsc.ac.cn";
const char* NTP_SERVER_3 = "pool.ntp.org";

const int BUTTON_PIN = 0;
const int PUMP_PIN = 23;

WebServer server(80);

bool pumpState = false;
bool lastButtonState = HIGH;

bool autoStopEnabled = false;
unsigned long autoStopAt = 0;
unsigned long lastPumpStartAt = 0;

unsigned long totalWaterMs = 0;
unsigned int waterCount = 0;

// ------------------------------
// 模拟土壤湿度
// ------------------------------
float simulatedSoilMoisture = 56.0f;
unsigned long lastSoilUpdateAt = 0;

// ------------------------------
// 定时浇水配置
// intervalDays = 1 表示每天
// intervalDays = 2 表示每隔 2 天
// anchorEpochDay = 保存时的“基准日”
// lastRunEpochDay = 上次执行的日期，防止同一天重复触发
// ------------------------------
struct ScheduleConfig {
  bool enabled;
  int hour;
  int minute;
  int intervalDays;
  int durationSec;
  long anchorEpochDay;
  long lastRunEpochDay;
};

ScheduleConfig gSchedule = {
  false,   // enabled
  9,       // hour
  0,       // minute
  1,       // intervalDays
  5,       // durationSec
  -1,      // anchorEpochDay
  -1       // lastRunEpochDay
};

// ==============================
// 工具函数
// ==============================
void applyPumpState() {
  digitalWrite(PUMP_PIN, pumpState ? HIGH : LOW);
}

String formatDuration(unsigned long ms) {
  unsigned long sec = ms / 1000;
  unsigned long h = sec / 3600;
  unsigned long m = (sec % 3600) / 60;
  unsigned long s = sec % 60;

  char buf[32];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", h, m, s);
  return String(buf);
}

String currentUptimeText() {
  return formatDuration(millis());
}

String currentRemainingText() {
  if (!pumpState || !autoStopEnabled) {
    return "-";
  }

  long remain = (long)(autoStopAt - millis());
  if (remain < 0) remain = 0;

  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f s", remain / 1000.0);
  return String(buf);
}

String currentTotalWaterText() {
  unsigned long currentTotal = totalWaterMs;

  if (pumpState) {
    unsigned long now = millis();
    if (now >= lastPumpStartAt) {
      currentTotal += (now - lastPumpStartAt);
    }
  }

  return formatDuration(currentTotal);
}

// ==============================
// 时间相关
// ==============================
bool getBeijingTime(struct tm* timeinfo) {
  return getLocalTime(timeinfo, 100);
}

bool isTimeSynced() {
  struct tm timeinfo;
  return getBeijingTime(&timeinfo);
}

String currentBeijingTimeText() {
  struct tm timeinfo;
  if (!getBeijingTime(&timeinfo)) {
    return "未同步";
  }

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}

long currentEpochDay() {
  struct tm timeinfo;
  if (!getBeijingTime(&timeinfo)) {
    return -1;
  }

  time_t t = mktime(&timeinfo);
  return (long)(t / 86400);
}

String scheduleModeText() {
  if (!gSchedule.enabled) {
    return "未启用";
  }

  if (gSchedule.intervalDays <= 1) {
    return "每天";
  }

  return "每隔 " + String(gSchedule.intervalDays) + " 天";
}

String scheduleSummaryText() {
  if (!gSchedule.enabled) {
    return "当前未启用自动浇水";
  }

  char hm[16];
  snprintf(hm, sizeof(hm), "%02d:%02d", gSchedule.hour, gSchedule.minute);

  String text = scheduleModeText();
  text += " ";
  text += String(hm);
  text += " 浇水 ";
  text += String(gSchedule.durationSec);
  text += " 秒";

  return text;
}

String scheduleLastRunText() {
  if (gSchedule.lastRunEpochDay < 0) {
    return "暂无";
  }

  return "第 " + String(gSchedule.lastRunEpochDay) + " 天已执行";
}

// ==============================
// 湿度模拟
// ==============================
void updateSimulatedSoilMoisture() {
  unsigned long now = millis();

  if (lastSoilUpdateAt == 0) {
    lastSoilUpdateAt = now;
    return;
  }

  unsigned long delta = now - lastSoilUpdateAt;
  if (delta < 1000) {
    return;
  }

  lastSoilUpdateAt = now;

  if (pumpState) {
    simulatedSoilMoisture += 1.3f;
  } else {
    simulatedSoilMoisture -= 0.18f;
  }

  if (simulatedSoilMoisture > 95.0f) simulatedSoilMoisture = 95.0f;
  if (simulatedSoilMoisture < 18.0f) simulatedSoilMoisture = 18.0f;
}

int currentSoilMoisturePercent() {
  int value = (int)(simulatedSoilMoisture + 0.5f);
  if (value < 0) value = 0;
  if (value > 100) value = 100;
  return value;
}

String soilLevelText(int moisture) {
  if (moisture >= 70) return "偏湿";
  if (moisture >= 45) return "适中";
  if (moisture >= 30) return "偏干";
  return "很干";
}

// ==============================
// 浇水控制
// ==============================
void stopWatering(const String& reason) {
  if (pumpState) {
    unsigned long now = millis();
    if (now >= lastPumpStartAt) {
      totalWaterMs += (now - lastPumpStartAt);
    }
  }

  pumpState = false;
  autoStopEnabled = false;
  autoStopAt = 0;
  applyPumpState();

  Serial.print("Pump OFF | reason = ");
  Serial.println(reason);
}

void startWatering(unsigned long durationMs, const String& reason) {
  if (!pumpState) {
    waterCount++;
    lastPumpStartAt = millis();
  } else {
    unsigned long now = millis();
    if (now >= lastPumpStartAt) {
      totalWaterMs += (now - lastPumpStartAt);
    }
    lastPumpStartAt = now;
  }

  pumpState = true;
  applyPumpState();

  if (durationMs > 0) {
    autoStopEnabled = true;
    autoStopAt = millis() + durationMs;
  } else {
    autoStopEnabled = false;
    autoStopAt = 0;
  }

  Serial.print("Pump ON | reason = ");
  Serial.print(reason);
  Serial.print(" | durationMs = ");
  Serial.println(durationMs);
}

// ==============================
// 定时任务检查
// ==============================
void checkScheduleTask() {
  if (!gSchedule.enabled) return;
  if (!isTimeSynced()) return;

  struct tm timeinfo;
  if (!getBeijingTime(&timeinfo)) return;

  long epochDay = currentEpochDay();
  if (epochDay < 0) return;

  if (gSchedule.anchorEpochDay < 0) {
    gSchedule.anchorEpochDay = epochDay;
  }

  if (timeinfo.tm_hour != gSchedule.hour) return;
  if (timeinfo.tm_min != gSchedule.minute) return;

  if (epochDay == gSchedule.lastRunEpochDay) {
    return;
  }

  long dayDelta = epochDay - gSchedule.anchorEpochDay;
  if (dayDelta < 0) return;

  int intervalDays = gSchedule.intervalDays <= 0 ? 1 : gSchedule.intervalDays;

  if ((dayDelta % intervalDays) != 0) {
    return;
  }

  gSchedule.lastRunEpochDay = epochDay;
  startWatering((unsigned long)gSchedule.durationSec * 1000UL, "schedule task");

  Serial.println("Schedule triggered!");
}

// ==============================
// JSON 状态
// ==============================
String jsonStatus() {
  int soil = currentSoilMoisturePercent();

  String json = "{";
  json += "\"pump\":\"";
  json += (pumpState ? "on" : "off");
  json += "\",";
  json += "\"remaining\":\"";
  json += currentRemainingText();
  json += "\",";
  json += "\"count\":";
  json += String(waterCount);
  json += ",";
  json += "\"total\":\"";
  json += currentTotalWaterText();
  json += "\",";
  json += "\"uptime\":\"";
  json += currentUptimeText();
  json += "\",";
  json += "\"ip\":\"";
  json += WiFi.localIP().toString();
  json += "\",";
  json += "\"ssid\":\"";
  json += WiFi.SSID();
  json += "\",";
  json += "\"beijingTime\":\"";
  json += currentBeijingTimeText();
  json += "\",";
  json += "\"timeSynced\":";
  json += (isTimeSynced() ? "true" : "false");
  json += ",";
  json += "\"soil\":";
  json += String(soil);
  json += ",";
  json += "\"soilText\":\"";
  json += soilLevelText(soil);
  json += "\",";
  json += "\"soilSimulated\":true,";
  json += "\"scheduleEnabled\":";
  json += (gSchedule.enabled ? "true" : "false");
  json += ",";
  json += "\"scheduleHour\":";
  json += String(gSchedule.hour);
  json += ",";
  json += "\"scheduleMinute\":";
  json += String(gSchedule.minute);
  json += ",";
  json += "\"scheduleIntervalDays\":";
  json += String(gSchedule.intervalDays);
  json += ",";
  json += "\"scheduleDurationSec\":";
  json += String(gSchedule.durationSec);
  json += ",";
  json += "\"scheduleSummary\":\"";
  json += scheduleSummaryText();
  json += "\"";
  json += "}";

  return json;
}

// ==============================
// 页面
// ==============================
String htmlPage() {
  int soil = currentSoilMoisturePercent();
  String soilText = soilLevelText(soil);
  String stateText = pumpState ? "RUNNING" : "STOPPED";
  String badgeClass = pumpState ? "running" : "stopped";
  String timeText = currentBeijingTimeText();
  String ssidText = WiFi.SSID();
  String scheduleText = scheduleSummaryText();

  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>MustangYM 浇水控制台</title>
  <style>
    :root {
      --bg1: #0f172a;
      --bg2: #111827;
      --card: rgba(255,255,255,0.08);
      --cardBorder: rgba(255,255,255,0.12);
      --text: #e5e7eb;
      --sub: #94a3b8;
      --green: #22c55e;
      --red: #ef4444;
      --blue: #38bdf8;
      --purple: #8b5cf6;
      --yellow: #f59e0b;
      --shadow: 0 20px 50px rgba(0,0,0,0.35);
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      font-family: -apple-system, BlinkMacSystemFont, "SF Pro Display", "Segoe UI", Roboto, sans-serif;
      color: var(--text);
      background:
        radial-gradient(circle at top left, rgba(56,189,248,0.18), transparent 30%),
        radial-gradient(circle at top right, rgba(139,92,246,0.18), transparent 25%),
        linear-gradient(160deg, var(--bg1), var(--bg2));
      min-height: 100vh;
      padding: 18px;
    }

    .wrap {
      max-width: 980px;
      margin: 0 auto;
    }

    .hero {
      padding: 18px 18px 8px;
    }

    .title {
      font-size: 30px;
      font-weight: 800;
      letter-spacing: 0.2px;
      margin: 0 0 8px 0;
    }

    .desc {
      color: var(--sub);
      font-size: 14px;
      line-height: 1.7;
      margin-bottom: 16px;
    }

    .grid {
      display: grid;
      gap: 16px;
      grid-template-columns: 1.3fr 1fr;
    }

    .card {
      background: var(--card);
      border: 1px solid var(--cardBorder);
      backdrop-filter: blur(14px);
      -webkit-backdrop-filter: blur(14px);
      border-radius: 22px;
      box-shadow: var(--shadow);
      padding: 18px;
    }

    .statusTop {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 14px;
      gap: 10px;
      flex-wrap: wrap;
    }

    .badge {
      display: inline-flex;
      align-items: center;
      gap: 8px;
      border-radius: 999px;
      padding: 8px 14px;
      font-weight: 700;
      font-size: 14px;
      color: white;
      transition: all 0.2s ease;
    }

    .badge.running {
      background: linear-gradient(135deg, #16a34a, #22c55e);
      box-shadow: 0 0 0 6px rgba(34,197,94,0.12);
    }

    .badge.stopped {
      background: linear-gradient(135deg, #6b7280, #94a3b8);
      box-shadow: 0 0 0 6px rgba(148,163,184,0.10);
    }

    .pill {
      color: #cbd5e1;
      background: rgba(255,255,255,0.06);
      border: 1px solid rgba(255,255,255,0.08);
      border-radius: 999px;
      padding: 8px 12px;
      font-size: 13px;
    }

    .metrics {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
      margin-top: 14px;
    }

    .metric {
      background: rgba(255,255,255,0.05);
      border-radius: 16px;
      padding: 14px;
      border: 1px solid rgba(255,255,255,0.07);
    }

    .metricLabel {
      color: var(--sub);
      font-size: 12px;
      margin-bottom: 8px;
    }

    .metricValue {
      font-size: 22px;
      font-weight: 800;
      line-height: 1.2;
    }

    .metricSub {
      color: #cbd5e1;
      margin-top: 6px;
      font-size: 13px;
    }

    .soilCard {
      display: flex;
      flex-direction: column;
      gap: 14px;
    }

    .soilValue {
      font-size: 38px;
      font-weight: 900;
      line-height: 1;
    }

    .barWrap {
      width: 100%;
      height: 16px;
      background: rgba(255,255,255,0.08);
      border-radius: 999px;
      overflow: hidden;
      border: 1px solid rgba(255,255,255,0.08);
    }

    .bar {
      height: 100%;
      width: 0%;
      border-radius: 999px;
      background: linear-gradient(90deg, #f59e0b, #38bdf8, #22c55e);
      transition: width 0.35s ease;
    }

    .buttons {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
      margin-top: 16px;
    }

    .btn {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      text-decoration: none;
      min-height: 54px;
      border-radius: 16px;
      font-weight: 800;
      font-size: 15px;
      color: white;
      transition: transform 0.15s ease, opacity 0.15s ease, filter 0.15s ease;
      border: none;
      cursor: pointer;
      width: 100%;
      -webkit-tap-highlight-color: transparent;
    }

    .btn:active {
      transform: scale(0.98);
      opacity: 0.92;
    }

    .btn:disabled {
      opacity: 0.65;
      filter: grayscale(0.1);
      cursor: wait;
    }

    .btn-blue { background: linear-gradient(135deg, #0284c7, #38bdf8); }
    .btn-green { background: linear-gradient(135deg, #16a34a, #22c55e); }
    .btn-purple { background: linear-gradient(135deg, #7c3aed, #8b5cf6); }
    .btn-red { background: linear-gradient(135deg, #dc2626, #ef4444); }
    .btn-gray { background: linear-gradient(135deg, #374151, #6b7280); }

    .footer {
      margin-top: 18px;
      color: var(--sub);
      font-size: 13px;
      line-height: 1.7;
    }

    .tag {
      display: inline-block;
      padding: 4px 10px;
      border-radius: 999px;
      background: rgba(245,158,11,0.14);
      color: #fbbf24;
      font-size: 12px;
      font-weight: 700;
      margin-left: 8px;
      vertical-align: middle;
    }

    .toast {
      position: fixed;
      left: 50%;
      bottom: 26px;
      transform: translateX(-50%) translateY(20px);
      background: rgba(17,24,39,0.92);
      color: white;
      padding: 10px 14px;
      border-radius: 12px;
      font-size: 14px;
      opacity: 0;
      pointer-events: none;
      transition: all 0.22s ease;
      z-index: 9999;
      border: 1px solid rgba(255,255,255,0.08);
    }

    .toast.show {
      opacity: 1;
      transform: translateX(-50%) translateY(0);
    }

    .formGrid {
      display: grid;
      grid-template-columns: repeat(2, minmax(0, 1fr));
      gap: 12px;
      margin-top: 12px;
    }

    .field {
      display: flex;
      flex-direction: column;
      gap: 8px;
    }

    .label {
      color: var(--sub);
      font-size: 12px;
    }

    .input, .checkboxWrap {
      width: 100%;
      border: 1px solid rgba(255,255,255,0.08);
      background: rgba(255,255,255,0.05);
      color: white;
      border-radius: 12px;
      padding: 12px;
      font-size: 15px;
      outline: none;
    }

    .checkboxWrap {
      display: flex;
      align-items: center;
      gap: 10px;
    }

    .full {
      grid-column: 1 / -1;
    }

    @media (max-width: 820px) {
      .grid {
        grid-template-columns: 1fr;
      }
      .title {
        font-size: 26px;
      }
      .formGrid {
        grid-template-columns: 1fr;
      }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="hero">
      <div class="title">MustangYM 浇水控制台</div>
      <div class="desc">
        支持手动浇水、自动定时浇水、北京时间同步、Wi-Fi 信息显示。
      </div>
    </div>

    <div class="grid">
      <div class="card">
        <div class="statusTop">
          <div id="pumpBadge" class="badge %BADGE_CLASS%">%STATE%</div>
          <div class="pill">ESP32 IP：<span id="ipText">%IP%</span></div>
        </div>

        <div class="metrics">
          <div class="metric">
            <div class="metricLabel">北京时间</div>
            <div id="timeText" class="metricValue">%TIME%</div>
            <div id="timeSyncText" class="metricSub">时间同步状态检查中...</div>
          </div>

          <div class="metric">
            <div class="metricLabel">当前 Wi-Fi</div>
            <div id="ssidText" class="metricValue">%SSID%</div>
            <div class="metricSub">当前连接的局域网</div>
          </div>

          <div class="metric">
            <div class="metricLabel">剩余时间</div>
            <div id="remainingText" class="metricValue">%REMAIN%</div>
            <div class="metricSub">定时浇水时自动倒计时</div>
          </div>

          <div class="metric">
            <div class="metricLabel">累计浇水次数</div>
            <div id="countText" class="metricValue">%COUNT%</div>
            <div class="metricSub">每次启动浇水 +1</div>
          </div>

          <div class="metric">
            <div class="metricLabel">累计浇水总时长</div>
            <div id="totalText" class="metricValue">%TOTAL%</div>
            <div class="metricSub">用于后期做统计报表</div>
          </div>

          <div class="metric">
            <div class="metricLabel">设备运行时长</div>
            <div id="uptimeText" class="metricValue">%UPTIME%</div>
            <div class="metricSub">从本次启动开始累计</div>
          </div>
        </div>

        <div class="buttons">
          <button class="btn btn-blue" onclick="runAction('/water?sec=3', '已触发浇水 3 秒')">浇水 3 秒</button>
          <button class="btn btn-purple" onclick="runAction('/water?sec=5', '已触发浇水 5 秒')">浇水 5 秒</button>
          <button class="btn btn-green" onclick="runAction('/on', '已手动开启')">手动开启</button>
          <button class="btn btn-red" onclick="runAction('/off', '已立即停止')">立即停止</button>
          <button class="btn btn-gray" onclick="runAction('/reset', '统计已清零')">清零统计</button>
          <button class="btn btn-gray" onclick="refreshStatus(true)">刷新页面</button>
        </div>
      </div>

      <div class="card soilCard">
        <div>
          <div class="metricLabel">土壤湿度 <span class="tag">实时数据</span></div>
          <div id="soilValue" class="soilValue">%SOIL%%</div>
          <div id="soilText" class="metricSub">%SOIL_TEXT%</div>
        </div>

        <div class="barWrap">
          <div id="soilBar" class="bar" style="width:%SOIL%%;"></div>
        </div>

        <div class="metric">
          <div class="metricLabel">当前定时计划</div>
          <div id="scheduleSummary" class="metricValue" style="font-size:18px;">%SCHEDULE_TEXT%</div>
          <div class="metricSub">保存后立即生效</div>
        </div>

        <div class="formGrid">
          <div class="field full">
            <label class="checkboxWrap">
              <input id="scheduleEnabled" type="checkbox" />
              <span>启用自动定时浇水</span>
            </label>
          </div>

          <div class="field">
            <label class="label">小时（0-23）</label>
            <input id="scheduleHour" class="input" type="number" min="0" max="23" value="9" />
          </div>

          <div class="field">
            <label class="label">分钟（0-59）</label>
            <input id="scheduleMinute" class="input" type="number" min="0" max="59" value="0" />
          </div>

          <div class="field">
            <label class="label">间隔天数</label>
            <input id="scheduleIntervalDays" class="input" type="number" min="1" max="365" value="1" />
          </div>

          <div class="field">
            <label class="label">每次浇水秒数</label>
            <input id="scheduleDurationSec" class="input" type="number" min="1" max="600" value="5" />
          </div>

          <div class="field full">
            <button class="btn btn-blue" onclick="saveSchedule()">保存定时计划</button>
          </div>
        </div>
      </div>
    </div>

    <div class="footer">
      Copyright © 2026年 MustangYM. All rights reserved.<br/>
    </div>
  </div>

  <div id="toast" class="toast"></div>

  <script>
  let actionBusy = false;

  // 是否已经用服务端数据初始化过表单
  let scheduleFormInitialized = false;

  // 用户是否正在编辑定时面板
  let scheduleFormDirty = false;

  function showToast(msg) {
    const toast = document.getElementById('toast');
    toast.textContent = msg;
    toast.classList.add('show');
    clearTimeout(showToast._timer);
    showToast._timer = setTimeout(() => {
      toast.classList.remove('show');
    }, 1500);
  }

  function setButtonsDisabled(disabled) {
    document.querySelectorAll('.buttons .btn').forEach(btn => {
      btn.disabled = disabled;
    });
  }

  function scheduleInputs() {
    return [
      document.getElementById('scheduleEnabled'),
      document.getElementById('scheduleHour'),
      document.getElementById('scheduleMinute'),
      document.getElementById('scheduleIntervalDays'),
      document.getElementById('scheduleDurationSec')
    ];
  }

  function bindScheduleEditEvents() {
    scheduleInputs().forEach(el => {
      el.addEventListener('input', () => {
        scheduleFormDirty = true;
      });
      el.addEventListener('change', () => {
        scheduleFormDirty = true;
      });
    });
  }

  function fillScheduleForm(data, force = false) {
    // 用户正在编辑时，不要自动覆盖
    if (!force && scheduleFormInitialized && scheduleFormDirty) {
      return;
    }

    document.getElementById('scheduleEnabled').checked = !!data.scheduleEnabled;
    document.getElementById('scheduleHour').value = data.scheduleHour;
    document.getElementById('scheduleMinute').value = data.scheduleMinute;
    document.getElementById('scheduleIntervalDays').value = data.scheduleIntervalDays;
    document.getElementById('scheduleDurationSec').value = data.scheduleDurationSec;

    scheduleFormInitialized = true;
    scheduleFormDirty = false;
  }

  async function refreshStatus(showMsg = false) {
    try {
      const res = await fetch('/status?_t=' + Date.now(), {
        method: 'GET',
        cache: 'no-store'
      });
      const data = await res.json();

      const badge = document.getElementById('pumpBadge');
      badge.textContent = data.pump === 'on' ? 'RUNNING' : 'STOPPED';
      badge.className = 'badge ' + (data.pump === 'on' ? 'running' : 'stopped');

      document.getElementById('remainingText').textContent = data.remaining;
      document.getElementById('countText').textContent = data.count;
      document.getElementById('totalText').textContent = data.total;
      document.getElementById('uptimeText').textContent = data.uptime;
      document.getElementById('ipText').textContent = data.ip;
      document.getElementById('ssidText').textContent = data.ssid || '-';
      document.getElementById('timeText').textContent = data.beijingTime || '未同步';
      document.getElementById('timeSyncText').textContent = data.timeSynced
        ? 'NTP 已同步，可用于定时浇水'
        : '时间未同步，定时浇水不可靠';

      document.getElementById('soilValue').textContent = data.soil + '%';
      document.getElementById('soilText').textContent = data.soilText;
      document.getElementById('soilBar').style.width = data.soil + '%';

      document.getElementById('scheduleSummary').textContent =
        data.scheduleSummary || '当前未启用自动浇水';

      // 只有第一次加载，或者用户没在编辑时，才同步表单
      fillScheduleForm(data, false);

      if (showMsg) {
        showToast('状态已刷新');
      }
    } catch (err) {
      console.log('refresh failed', err);
    }
  }

  async function runAction(url, okMsg) {
    if (actionBusy) return;

    actionBusy = true;
    setButtonsDisabled(true);

    try {
      await fetch(url + (url.includes('?') ? '&' : '?') + '_t=' + Date.now(), {
        method: 'GET',
        cache: 'no-store'
      });

      await refreshStatus(false);
      showToast(okMsg);

      setTimeout(() => refreshStatus(false), 120);
      setTimeout(() => refreshStatus(false), 500);
    } catch (err) {
      console.log('action failed', err);
      showToast('请求失败');
    } finally {
      setButtonsDisabled(false);
      actionBusy = false;
    }
  }

  async function saveSchedule() {
    const enabled = document.getElementById('scheduleEnabled').checked ? 1 : 0;
    const hour = parseInt(document.getElementById('scheduleHour').value || '0', 10);
    const minute = parseInt(document.getElementById('scheduleMinute').value || '0', 10);
    const intervalDays = parseInt(document.getElementById('scheduleIntervalDays').value || '1', 10);
    const durationSec = parseInt(document.getElementById('scheduleDurationSec').value || '5', 10);

    const url = `/schedule/set?enabled=${enabled}&hour=${hour}&minute=${minute}&intervalDays=${intervalDays}&durationSec=${durationSec}`;

    try {
      const res = await fetch(url + '&_t=' + Date.now(), {
        method: 'GET',
        cache: 'no-store'
      });
      const text = await res.text();

      if (text === 'OK') {
        scheduleFormDirty = false;
        await refreshStatus(false);
        fillScheduleForm({
          scheduleEnabled: !!enabled,
          scheduleHour: hour,
          scheduleMinute: minute,
          scheduleIntervalDays: intervalDays,
          scheduleDurationSec: durationSec
        }, true);
        showToast('定时计划已保存');
      } else {
        showToast('保存失败');
      }
    } catch (err) {
      console.log(err);
      showToast('请求失败');
    }
  }

  bindScheduleEditEvents();
  refreshStatus(false);

  setInterval(() => {
    if (!actionBusy) {
      refreshStatus(false);
    }
  }, 1000);
</script>
</body>
</html>
)rawliteral";

  html.replace("%STATE%", stateText);
  html.replace("%BADGE_CLASS%", badgeClass);
  html.replace("%IP%", WiFi.localIP().toString());
  html.replace("%TIME%", timeText);
  html.replace("%SSID%", ssidText);
  html.replace("%REMAIN%", currentRemainingText());
  html.replace("%COUNT%", String(waterCount));
  html.replace("%TOTAL%", currentTotalWaterText());
  html.replace("%UPTIME%", currentUptimeText());
  html.replace("%SOIL%", String(soil));
  html.replace("%SOIL_TEXT%", soilText);
  html.replace("%SCHEDULE_TEXT%", scheduleText);

  return html;
}

// ==============================
// 接口
// ==============================
void handleRoot() {
  server.send(200, "text/html; charset=utf-8", htmlPage());
}

void handleOn() {
  startWatering(0, "web manual on");
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleOff() {
  stopWatering("web manual off");
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleWater() {
  if (!server.hasArg("sec")) {
    server.send(400, "text/plain; charset=utf-8", "missing sec");
    return;
  }

  int sec = server.arg("sec").toInt();
  if (sec <= 0) {
    server.send(400, "text/plain; charset=utf-8", "invalid sec");
    return;
  }

  startWatering((unsigned long)sec * 1000UL, "web timed watering");
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleReset() {
  if (pumpState) {
    stopWatering("reset stats");
  }
  totalWaterMs = 0;
  waterCount = 0;
  simulatedSoilMoisture = 56.0f;
  Serial.println("Stats reset.");
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleScheduleSet() {
  if (!server.hasArg("enabled") ||
      !server.hasArg("hour") ||
      !server.hasArg("minute") ||
      !server.hasArg("intervalDays") ||
      !server.hasArg("durationSec")) {
    server.send(400, "text/plain; charset=utf-8", "missing args");
    return;
  }

  int enabled = server.arg("enabled").toInt();
  int hour = server.arg("hour").toInt();
  int minute = server.arg("minute").toInt();
  int intervalDays = server.arg("intervalDays").toInt();
  int durationSec = server.arg("durationSec").toInt();

  if (hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 ||
      intervalDays < 1 || intervalDays > 365 ||
      durationSec < 1 || durationSec > 600) {
    server.send(400, "text/plain; charset=utf-8", "invalid args");
    return;
  }

  gSchedule.enabled = (enabled == 1);
  gSchedule.hour = hour;
  gSchedule.minute = minute;
  gSchedule.intervalDays = intervalDays;
  gSchedule.durationSec = durationSec;

  long today = currentEpochDay();
  gSchedule.anchorEpochDay = today;
  gSchedule.lastRunEpochDay = -1;

  Serial.println("Schedule updated:");
  Serial.println(scheduleSummaryText());

  server.send(200, "text/plain; charset=utf-8", "OK");
}

void handleStatus() {
  server.send(200, "application/json; charset=utf-8", jsonStatus());
}

// ==============================
// 网络
// ==============================
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to Wi-Fi");
  int retry = 0;

  while (WiFi.status() != WL_CONNECTED && retry < 40) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    WiFi.setSleep(false);
    Serial.println("Wi-Fi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
  } else {
    Serial.println("Wi-Fi connect failed.");
    Serial.println("Please check SSID / password.");
  }
}

void initBeijingTime() {
  setenv("TZ", TZ_INFO, 1);
  tzset();
  configTzTime(TZ_INFO, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);

  Serial.println("Syncing Beijing time...");
  for (int i = 0; i < 20; i++) {
    if (isTimeSynced()) {
      Serial.print("Time synced: ");
      Serial.println(currentBeijingTimeText());
      return;
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("Time sync not ready yet, will continue in background.");
}

// ==============================
// Arduino
// ==============================
void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(PUMP_PIN, OUTPUT);

  pumpState = false;
  applyPumpState();

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("ESP32 Smart Plant Watering Console");
  Serial.println("BOOT button = toggle simulated watering");
  Serial.println("================================");

  connectWiFi();
  initBeijingTime();

  server.on("/", handleRoot);
  server.on("/on", handleOn);
  server.on("/off", handleOff);
  server.on("/water", handleWater);
  server.on("/reset", handleReset);
  server.on("/schedule/set", handleScheduleSet);
  server.on("/status", handleStatus);

  server.begin();
  Serial.println("HTTP server started.");
  Serial.println("Open the IP above in your phone browser.");
}

void loop() {
  server.handleClient();

  updateSimulatedSoilMoisture();
  checkScheduleTask();

  if (pumpState && autoStopEnabled && millis() >= autoStopAt) {
    stopWatering("auto timeout");
  }

  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && currentButtonState == LOW) {
    if (pumpState) {
      stopWatering("BOOT button");
    } else {
      startWatering(3000, "BOOT button");
    }
    delay(150);
  }

  lastButtonState = currentButtonState;

  delay(10);
}