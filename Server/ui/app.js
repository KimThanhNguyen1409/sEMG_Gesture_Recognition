/* ==========================================================================
   sEMG IoT Control Center — Frontend Controller (Vanilla JS)
   ========================================================================== */

const API_BASE = window.location.protocol === 'file:' ? 'http://127.0.0.1:8080' : '';
const RADIAL_CIRCUMFERENCE = 263.89; // 2 * PI * 42

let isConfigSynced = false;
let simSequence = 100;
let lastKnownGesture = -1;

// SVG Icons cho 4 Cử chỉ Bàn tay
const HAND_SVGS = {
  RESTING: `
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
      <path d="M18 11V6a2 2 0 0 0-2-2v0a2 2 0 0 0-2 2v5"/>
      <path d="M14 10V4a2 2 0 0 0-2-2v0a2 2 0 0 0-2 2v8"/>
      <path d="M10 10.5V6a2 2 0 0 0-2-2v0a2 2 0 0 0-2 2v8"/>
      <path d="M18 8a2 2 0 1 1 4 0v6a8 8 0 0 1-8 8h-2c-2.8 0-4.5-.86-5.99-2.34l-3.6-3.6a2 2 0 0 1 2.83-2.82L7 13"/>
    </svg>
  `,
  FIST: `
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
      <path d="M8 6a2 2 0 0 1 2-2h0a2 2 0 0 1 2 2v3"/>
      <path d="M12 5a2 2 0 0 1 2-2h0a2 2 0 0 1 2 2v4"/>
      <path d="M16 6a2 2 0 0 1 2-2h0a2 2 0 0 1 2 2v4"/>
      <path d="M6 9a2 2 0 0 1 2-2h10a3 3 0 0 1 3 3v4a6 6 0 0 1-6 6H9a4 4 0 0 1-4-4V11a2 2 0 0 1 1-2"/>
      <path d="M5 12h14"/>
      <path d="M5 15h10"/>
      <path d="M8 18v2"/>
      <path d="M14 18v2"/>
    </svg>
  `,
  OPEN_HAND: `
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
      <path d="M18 11V6a2 2 0 0 0-4 0v5"/>
      <path d="M14 10V4a2 2 0 0 0-4 0v7"/>
      <path d="M10 10.5V6a2 2 0 0 0-4 0v8"/>
      <path d="M18 8a2 2 0 1 1 4 0v6a8 8 0 0 1-8 8h-2c-2.8 0-4.5-.86-5.99-2.34l-3.6-3.6a2 2 0 0 1 2.83-2.82L7 15"/>
    </svg>
  `,
  POINTING: `
    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round">
      <path d="M10 12V3a2 2 0 1 1 4 0v9"/>
      <path d="M14 9a2 2 0 0 1 2 2v1"/>
      <path d="M16 11a2 2 0 0 1 2 2v2"/>
      <path d="M18 12a2 2 0 0 1 2 2v2a6 6 0 0 1-6 6h-2c-2.8 0-4.5-.86-5.99-2.34l-2.6-2.6a2 2 0 0 1 2.83-2.82L10 14"/>
    </svg>
  `
};

const SIM_PRESETS = {
  RESTING:   [0.001347, 0.002253, 0.265422, 75.0, 133.0, 0.001073, 0.001286, 0.223943, 62.0, 90.0, 0.000911, 0.001153, 0.238625, 92.0, 104.0],
  FIST:      [0.014540, 0.021677, 1.848793, 47.0,  91.0, 0.006135, 0.008134, 0.931958, 41.0, 91.0, 0.004038, 0.005197, 0.674616, 54.0,  94.0],
  OPEN_HAND: [0.006292, 0.009418, 1.004881, 44.0,  60.0, 0.020529, 0.032257, 3.061297, 44.0, 71.0, 0.021754, 0.027332, 3.174976, 49.0,  72.0],
  POINTING:  [0.016521, 0.020724, 2.022189, 41.0,  92.0, 0.027749, 0.036268, 3.174164, 35.0, 65.0, 0.021207, 0.027706, 3.095427, 43.0,  67.0]
};

// Render Micro Telemetry cho 1 Kênh (MAV, RMS, WL kèm mini bar; ZC, SSC dạng chip)
function renderChannelTelemetry(slotId, feats, offset) {
  const container = document.getElementById(slotId);
  if (!container) return;

  const mav = feats[offset + 0] || 0;
  const rms = feats[offset + 1] || 0;
  const wl  = feats[offset + 2] || 0;
  const zc  = feats[offset + 3] || 0;
  const ssc = feats[offset + 4] || 0;

  // Giới hạn max tương đối để vẽ micro-bar
  const mavPct = Math.min(100, Math.max(0, (mav / 0.025) * 100));
  const rmsPct = Math.min(100, Math.max(0, (rms / 0.035) * 100));
  const wlPct  = Math.min(100, Math.max(0, (wl / 3.0) * 100));

  container.innerHTML = `
    <div class="metric-micro-row">
      <span class="metric-micro-tag">MAV</span>
      <div class="metric-micro-track"><div class="metric-micro-fill" style="width: ${mavPct.toFixed(1)}%"></div></div>
      <span class="metric-micro-val">${mav.toFixed(5)}</span>
    </div>
    <div class="metric-micro-row">
      <span class="metric-micro-tag">RMS</span>
      <div class="metric-micro-track"><div class="metric-micro-fill" style="width: ${rmsPct.toFixed(1)}%"></div></div>
      <span class="metric-micro-val">${rms.toFixed(5)}</span>
    </div>
    <div class="metric-micro-row">
      <span class="metric-micro-tag">WL</span>
      <div class="metric-micro-track"><div class="metric-micro-fill" style="width: ${wlPct.toFixed(1)}%"></div></div>
      <span class="metric-micro-val">${wl.toFixed(3)}</span>
    </div>
    <div class="freq-chips-row">
      <div class="freq-chip"><span>Zero-Cross (ZC)</span><strong>${zc.toFixed(0)}</strong></div>
      <div class="freq-chip"><span>Slope-Change (SSC)</span><strong>${ssc.toFixed(0)}</strong></div>
    </div>
  `;
}

// Cập nhật Vòng tròn Radial Confidence Gauge
function updateRadialConfidence(conf) {
  const pct = Math.max(0, Math.min(1, conf));
  const circle = document.getElementById('radialCircle');
  const text = document.getElementById('radialPercent');
  const offset = RADIAL_CIRCUMFERENCE - (pct * RADIAL_CIRCUMFERENCE);

  if (circle) {
    circle.style.strokeDashoffset = offset.toFixed(1);
    if (pct >= 0.75) {
      circle.style.stroke = '#2563eb';
    } else {
      circle.style.stroke = '#f59e0b';
    }
  }
  if (text) {
    text.textContent = `${Math.round(pct * 100)}%`;
  }
}

// Cập nhật SVG Hero Gesture
function updateHeroGesture(label, action, conf) {
  const titleEl = document.getElementById('heroGestureText');
  const actionEl = document.getElementById('heroActionPill');
  const svgSlot = document.getElementById('handSvgSlot');
  const avatarBox = document.getElementById('handAvatarBox');

  const cleanLabel = (label || 'RESTING').toUpperCase();
  titleEl.textContent = cleanLabel;
  actionEl.textContent = `Lệnh: ${action || 'IDLE_HOLD'}`;

  // Gắn SVG bàn tay tương ứng
  svgSlot.innerHTML = HAND_SVGS[cleanLabel] || HAND_SVGS.RESTING;

  // Kích hoạt hiệu ứng pulse khi có cử chỉ tích cực
  if (cleanLabel !== 'RESTING') {
    avatarBox.classList.add('active-gesture');
  } else {
    avatarBox.classList.remove('active-gesture');
  }

  updateRadialConfidence(conf);
}

// Fetch State từ C Server
async function fetchServerState() {
  try {
    const res = await fetch(`${API_BASE}/api/state`);
    if (!res.ok) throw new Error('API offline');
    const data = await res.json();

    // 1. Header & Engine Info
    document.getElementById('engineInfo').textContent = `Engine: ${data.engine} • Uptime: ${data.uptime_sec}s`;
    const mqttChip = document.getElementById('mqttChip');
    const mqttText = document.getElementById('mqttStatusText');
    if (data.mqtt_connected) {
      mqttChip.className = 'status-chip online';
      mqttText.textContent = 'MQTT Broker: Sẵn sàng (Port 1883)';
    } else {
      mqttChip.className = 'status-chip';
      mqttText.textContent = 'MQTT Broker: Đang thử kết nối lại...';
    }
    document.getElementById('uptimeText').textContent = `Uptime: ${data.uptime_sec}s`;

    // 2. Slim Metric Strip
    document.getElementById('kpiEndDevOnline').textContent = data.enddev_online;
    document.getElementById('kpiEndDevTotal').textContent = data.enddev_total;
    document.getElementById('kpiSensorOnline').textContent = data.sensor_online;
    document.getElementById('kpiSensorTotal').textContent = data.sensor_total;
    document.getElementById('kpiTotalRx').textContent = data.total_rx;
    document.getElementById('kpiLastDev').textContent = data.latest.sensor_id ? `(${data.latest.sensor_id})` : 'gói';
    document.getElementById('kpiTotalTx').textContent = data.total_tx;

    // 3. Focal Hero Stage
    const currentAction = (data.config && data.config.actions) ? data.config.actions[data.latest.stable_gesture] : 'IDLE_HOLD';
    updateHeroGesture(data.latest.stable_label, currentAction, data.latest.conf);

    document.getElementById('activeSensorLabel').textContent = `Sensor: ${data.latest.sensor_id || 'none'}`;
    document.getElementById('activeSeqLabel').textContent = `seq #${data.latest.seq}`;

    // 4. MLP Probabilities Bar List
    const probs = data.latest.probs || [0, 0, 0, 0];
    let maxIdx = 0;
    let maxP = -1;
    for (let i = 0; i < 4; i++) {
      if (probs[i] > maxP) {
        maxP = probs[i];
        maxIdx = i;
      }
      const pct = (probs[i] * 100).toFixed(1);
      document.getElementById(`pct${i}`).textContent = `${pct}%`;
      document.getElementById(`fill${i}`).style.width = `${pct}%`;
      document.getElementById(`probItem${i}`).classList.remove('winner');
    }
    document.getElementById(`probItem${maxIdx}`).classList.add('winner');

    // 5. Telemetry 15 Features (3 Kênh)
    const feats = data.latest.features || [];
    renderChannelTelemetry('ch1Metrics', feats, 0);
    renderChannelTelemetry('ch2Metrics', feats, 5);
    renderChannelTelemetry('ch3Metrics', feats, 10);

    // 6. Cột 1: Danh sách Thiết bị
    const devContainer = document.getElementById('deviceListContainer');
    const devCount = (data.devices || []).length;
    document.getElementById('fleetBadge').textContent = `${devCount} thiết bị`;

    if (!data.devices || data.devices.length === 0) {
      devContainer.innerHTML = '<div class="empty-state">Chưa có thiết bị nào trong mạng</div>';
    } else {
      devContainer.innerHTML = data.devices.map(d => {
        const isOnline = d.online;
        const roleClass = d.role === 'SENSOR_MCU' ? 'sensor' : 'enddev';
        const roleName = d.role === 'SENSOR_MCU' ? 'Sensor ESP32' : 'End Device';
        return `
          <div class="device-item-card">
            <div class="dev-row-top">
              <span class="dev-name">${d.id}</span>
              <span class="dev-role-badge ${roleClass}">${roleName}</span>
            </div>
            <div class="dev-row-bottom">
              <span class="dev-status-pill ${isOnline ? 'online' : 'offline'}">
                ● ${isOnline ? 'ONLINE' : 'OFFLINE'}
              </span>
              <span class="dev-action-desc">${d.status || 'Active'}</span>
            </div>
          </div>
        `;
      }).join('');
    }

    // 7. Cột 1: Đồng bộ Form Config lần đầu
    if (!isConfigSynced && data.config) {
      isConfigSynced = true;
      document.getElementById('confInput').value = data.config.conf_threshold;
      document.getElementById('confDisplay').textContent = data.config.conf_threshold.toFixed(2);
      document.getElementById('voteInput').value = data.config.vote_window;
      document.getElementById('voteDisplay').textContent = `${data.config.vote_window} gói`;

      if (data.config.actions) {
        for (let i = 0; i < 4; i++) {
          const actInput = document.getElementById(`act${i}`);
          if (actInput) actInput.value = data.config.actions[i] || '';
        }
      }
    }

    // 8. Cột 3: Nhật Ký Gói Tin (Log Stream Feed)
    const logContainer = document.getElementById('logStreamContainer');
    if (!data.logs || data.logs.length === 0) {
      logContainer.innerHTML = '<div class="empty-state">Chưa nhận gói tin sEMG nào...</div>';
    } else {
      logContainer.innerHTML = data.logs.map(lg => {
        let badgeClass = 'rest';
        if (lg.stable_label === 'FIST') badgeClass = 'fist';
        else if (lg.stable_label === 'OPEN_HAND') badgeClass = 'open';
        else if (lg.stable_label === 'POINTING') badgeClass = 'point';

        return `
          <div class="log-entry-row">
            <div class="log-left">
              <span class="log-time">${lg.time}</span>
              <span class="log-dev">${lg.device_id}</span>
              <span class="log-seq">#${lg.seq}</span>
            </div>
            <div class="log-right">
              <span class="log-badge-gesture ${badgeClass}">${lg.stable_label}</span>
              <span class="log-conf">${(lg.conf * 100).toFixed(0)}%</span>
            </div>
          </div>
        `;
      }).join('');
    }

  } catch (err) {
    document.getElementById('engineInfo').textContent = 'Mất kết nối với C Server (Hãy chạy .\\run_server.bat)';
    const mqttChip = document.getElementById('mqttChip');
    mqttChip.className = 'status-chip';
    document.getElementById('mqttStatusText').textContent = 'MQTT Broker: Disconnected';
  }
}

// Lưu Cấu hình xuống C Server qua HTTP POST /api/config
async function saveConfig(e) {
  e.preventDefault();
  const payload = {
    conf_threshold: parseFloat(document.getElementById('confInput').value),
    vote_window: parseInt(document.getElementById('voteInput').value, 10),
    action_0: document.getElementById('act0').value.trim(),
    action_1: document.getElementById('act1').value.trim(),
    action_2: document.getElementById('act2').value.trim(),
    action_3: document.getElementById('act3').value.trim()
  };

  const statusToast = document.getElementById('saveStatus');
  try {
    const res = await fetch(`${API_BASE}/api/config`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    if (res.ok) {
      statusToast.textContent = '✓ Đã cập nhật thành công vào C Server!';
      setTimeout(() => { statusToast.textContent = ''; }, 2500);
      fetchServerState();
    }
  } catch (err) {
    statusToast.textContent = '✗ Không kết nối được C Server!';
  }
}

// Gửi mẫu sEMG mô phỏng
async function sendSimGesture(presetName) {
  const f = SIM_PRESETS[presetName] || SIM_PRESETS.RESTING;
  simSequence++;
  const payload = {
    device_id: 'esp32_sim_gui',
    seq: simSequence,
    f: f
  };
  await fetch(`${API_BASE}/api/simulate`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  fetchServerState();
}

// Thêm End Device mới
async function registerEndDevice() {
  const input = document.getElementById('newEndDevId');
  const devId = input.value.trim() || 'arm_robot_02';
  await fetch(`${API_BASE}/api/end_device`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: devId, status: 'ONLINE_READY' })
  });
  input.value = '';
  fetchServerState();
}

// Khởi chạy vòng lặp cập nhật mỗi 300ms
fetchServerState();
setInterval(fetchServerState, 300);
