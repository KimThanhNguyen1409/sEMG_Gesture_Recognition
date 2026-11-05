/* ==========================================================================
   sEMG IoT Control Center — Production Frontend Controller (Vanilla JS)
   Real-time SSE (<2ms), 2-Step MVC Calibration, Closed-Loop ACK Stepper
   ========================================================================== */

const API_BASE = window.location.protocol === 'file:' ? 'http://127.0.0.1:8080' : '';
const RADIAL_CIRCUMFERENCE = 263.89; // 2 * PI * 42

let isConfigSynced = false;
let simSequence = 100;
let currentCmdId = 0;
let eventSource = null;
let pollingTimer = null;

// SVG Icons cho 4 Cử chỉ Bàn tay (Chuẩn giải phẫu y sinh)
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
      <path d="M7 11V7a2 2 0 0 1 2-2h0a2 2 0 0 1 2 2v4"/>
      <path d="M11 10V6a2 2 0 0 1 2-2h0a2 2 0 0 1 2 2v4"/>
      <path d="M15 11V7a2 2 0 0 1 2-2h0a2 2 0 0 1 2 2v4"/>
      <path d="M5 11a2 2 0 0 1 2-2h11a3 3 0 0 1 3 3v4a6 6 0 0 1-6 6H9a4 4 0 0 1-4-4V11z"/>
      <path d="M5 14h15"/>
      <path d="M5 17h12"/>
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

// Vector 15 đặc trưng trích xuất từ tâm cụm thực tế trong tập dataset 300 trials
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
    circle.style.stroke = pct >= 0.75 ? '#2563eb' : '#f59e0b';
  }
  if (text) {
    text.textContent = `${Math.round(pct * 100)}%`;
  }
}

// Cập nhật Hero Gesture Visualizer
function updateHeroGesture(label, action, conf) {
  const titleEl = document.getElementById('heroGestureText');
  const actionEl = document.getElementById('heroActionPill');
  const svgSlot = document.getElementById('handSvgSlot');
  const avatarBox = document.getElementById('handAvatarBox');

  const cleanLabel = (label || 'RESTING').toUpperCase();
  if (titleEl) titleEl.textContent = cleanLabel;
  if (actionEl) actionEl.textContent = `Lệnh: ${action || 'IDLE_HOLD'}`;

  if (svgSlot) {
    svgSlot.innerHTML = HAND_SVGS[cleanLabel] || HAND_SVGS.RESTING;
  }

  if (avatarBox) {
    if (cleanLabel !== 'RESTING') {
      avatarBox.classList.add('active-gesture');
    } else {
      avatarBox.classList.remove('active-gesture');
    }
  }

  updateRadialConfidence(conf);
}

// Cập nhật Trạng thái Hiệu Chuẩn 2 Bước (MVC / Baseline)
function renderCalibrationUI(calib) {
  if (!calib) return;

  const calibBadge = document.getElementById('calibBadge');
  const progressBox = document.getElementById('calibProgressBox');
  const stepTitle = document.getElementById('calibStepTitle');
  const progressBar = document.getElementById('calibProgressBar');
  const progressText = document.getElementById('calibProgressText');
  const metricsSummary = document.getElementById('calibMetricsSummary');
  const btnStart = document.getElementById('btnStartCalib');

  const state = calib.state || 'IDLE';
  const isCalibrated = !!calib.is_calibrated;
  const progress = calib.progress || 0;

  // Cập nhật Badge
  if (calibBadge) {
    if (state === 'RESTING') {
      calibBadge.textContent = '1. Đo Baseline...';
      calibBadge.className = 'calib-status-badge resting';
    } else if (state === 'MVC') {
      calibBadge.textContent = '2. Đo MVC Peak...';
      calibBadge.className = 'calib-status-badge mvc';
    } else if (isCalibrated) {
      calibBadge.textContent = 'Calibrated (Active)';
      calibBadge.className = 'calib-status-badge calibrated';
    } else {
      calibBadge.textContent = 'Factory Scale';
      calibBadge.className = 'calib-status-badge';
    }
  }

  // Cập nhật Progress Box
  if (progressBox) {
    if (state === 'RESTING' || state === 'MVC') {
      progressBox.style.display = 'block';
      if (stepTitle) {
        stepTitle.textContent = state === 'RESTING'
          ? 'Bước 1: Giữ tay nghỉ thả lỏng (3s)...'
          : 'Bước 2: Nắm tay co cơ tối đa MVC (3s)...';
      }
      if (progressBar) progressBar.style.width = `${Math.min(100, Math.max(0, progress))}%`;
      if (progressText) progressText.textContent = `${progress}%`;
    } else {
      progressBox.style.display = 'none';
    }
  }

  // Cập nhật Hệ số Gain sau khi hiệu chuẩn
  if (metricsSummary) {
    if (isCalibrated && calib.gains) {
      metricsSummary.style.display = 'grid';
      const g1 = document.getElementById('gainCh1');
      const g2 = document.getElementById('gainCh2');
      const g3 = document.getElementById('gainCh3');
      if (g1) g1.textContent = `${calib.gains[0].toFixed(2)}x`;
      if (g2) g2.textContent = `${calib.gains[1].toFixed(2)}x`;
      if (g3) g3.textContent = `${calib.gains[2].toFixed(2)}x`;
    } else {
      metricsSummary.style.display = 'none';
    }
  }

  // Cập nhật Nút Bắt đầu
  if (btnStart) {
    if (state === 'RESTING' || state === 'MVC') {
      btnStart.disabled = true;
      btnStart.querySelector('span').textContent = 'Đang đo...';
    } else {
      btnStart.disabled = false;
      btnStart.querySelector('span').textContent = 'Bắt Đầu Hiệu Chuẩn (6s)';
    }
  }
}

// Cập nhật Timeline Closed-Loop Handshake ACK
function renderClosedLoopACK(ack) {
  if (!ack) return;

  currentCmdId = ack.cmd_id || 0;
  const cmdTag = document.getElementById('ackCmdTag');
  if (cmdTag) cmdTag.textContent = `cmd #${currentCmdId}`;

  const step1 = document.getElementById('stepDispatched');
  const conn1 = document.getElementById('conn1');
  const step2 = document.getElementById('stepExecuting');
  const conn2 = document.getElementById('conn2');
  const step3 = document.getElementById('stepConfirmed');
  const ackStatusLabel = document.getElementById('ackStatusLabel');
  const ackLatencyLabel = document.getElementById('ackLatencyLabel');

  if (!step1 || !step2 || !step3 || !conn1 || !conn2) return;

  step1.className = 'loop-step';
  conn1.className = 'loop-connector';
  step2.className = 'loop-step';
  conn2.className = 'loop-connector';
  step3.className = 'loop-step';

  const status = ack.status || 'IDLE';

  if (status === 'DISPATCHED') {
    step1.className = 'loop-step active dispatched';
    if (ackStatusLabel) ackStatusLabel.textContent = '3. Chờ phản hồi';
    if (ackLatencyLabel) ackLatencyLabel.textContent = 'Đang phát tín hiệu...';
  } else if (status === 'EXECUTING') {
    step1.className = 'loop-step active dispatched';
    conn1.className = 'loop-connector active';
    step2.className = 'loop-step active executing';
    if (ackStatusLabel) ackStatusLabel.textContent = '3. Đang thực thi';
    if (ackLatencyLabel) ackLatencyLabel.textContent = `Tại ${ack.target || 'End Device'}`;
  } else if (status === 'CONFIRMED') {
    step1.className = 'loop-step active dispatched';
    conn1.className = 'loop-connector active';
    step2.className = 'loop-step active dispatched';
    conn2.className = 'loop-connector active';
    step3.className = 'loop-step active confirmed';
    if (ackStatusLabel) ackStatusLabel.textContent = '3. CONFIRMED';
    if (ackLatencyLabel) ackLatencyLabel.textContent = `Độ trễ: ${ack.exec_time_ms || 0}ms`;
  } else if (status === 'TIMEOUT') {
    step1.className = 'loop-step active dispatched';
    conn1.className = 'loop-connector active';
    step2.className = 'loop-step active dispatched';
    conn2.className = 'loop-connector active';
    step3.className = 'loop-step active timeout';
    if (ackStatusLabel) ackStatusLabel.textContent = '3. TIMEOUT (>600ms)';
    if (ackLatencyLabel) ackLatencyLabel.textContent = 'Mất kết nối thiết bị';
  } else {
    if (ackStatusLabel) ackStatusLabel.textContent = '3. Sẵn sàng';
    if (ackLatencyLabel) ackLatencyLabel.textContent = 'Chờ lệnh tiếp theo';
  }
}

// Render toàn bộ UI từ JSON state
function renderAll(data) {
  if (!data) return;

  // 1. Header & Engine Info
  const engineInfo = document.getElementById('engineInfo');
  if (engineInfo) {
    engineInfo.textContent = `Engine: ${data.engine} • Uptime: ${data.uptime_sec}s`;
  }

  const mqttChip = document.getElementById('mqttChip');
  const mqttText = document.getElementById('mqttStatusText');
  if (mqttChip && mqttText) {
    if (data.mqtt_connected) {
      mqttChip.className = 'status-chip online';
      mqttText.textContent = 'MQTT Broker: Sẵn sàng (Port 1883)';
    } else {
      mqttChip.className = 'status-chip';
      mqttText.textContent = 'MQTT Broker: Đang thử kết nối lại...';
    }
  }

  const uptimeText = document.getElementById('uptimeText');
  if (uptimeText) uptimeText.textContent = `Uptime: ${data.uptime_sec}s`;

  // 2. Slim Metric Strip
  const kpiEndDevOnline = document.getElementById('kpiEndDevOnline');
  const kpiEndDevTotal = document.getElementById('kpiEndDevTotal');
  const kpiSensorOnline = document.getElementById('kpiSensorOnline');
  const kpiSensorTotal = document.getElementById('kpiSensorTotal');
  const kpiTotalRx = document.getElementById('kpiTotalRx');
  const kpiLastDev = document.getElementById('kpiLastDev');
  const kpiTotalTx = document.getElementById('kpiTotalTx');

  if (kpiEndDevOnline) kpiEndDevOnline.textContent = data.enddev_online;
  if (kpiEndDevTotal) kpiEndDevTotal.textContent = data.enddev_total;
  if (kpiSensorOnline) kpiSensorOnline.textContent = data.sensor_online;
  if (kpiSensorTotal) kpiSensorTotal.textContent = data.sensor_total;
  if (kpiTotalRx) kpiTotalRx.textContent = data.total_rx;
  if (kpiLastDev) kpiLastDev.textContent = data.latest.sensor_id ? `(${data.latest.sensor_id})` : 'gói';
  if (kpiTotalTx) kpiTotalTx.textContent = data.total_tx;

  // 3. Focal Hero Stage
  const currentAction = (data.config && data.config.actions) ? data.config.actions[data.latest.stable_gesture] : 'IDLE_HOLD';
  updateHeroGesture(data.latest.stable_label, currentAction, data.latest.conf);

  const activeSensorLabel = document.getElementById('activeSensorLabel');
  const activeSeqLabel = document.getElementById('activeSeqLabel');
  if (activeSensorLabel) activeSensorLabel.textContent = `Sensor: ${data.latest.sensor_id || 'none'}`;
  if (activeSeqLabel) activeSeqLabel.textContent = `seq #${data.latest.seq}`;

  // 4. Calibration & Closed-Loop ACK
  renderCalibrationUI(data.calib);
  renderClosedLoopACK(data.ack);

  // 5. MLP Probabilities Bar List
  const probs = data.latest.probs || [0, 0, 0, 0];
  let maxIdx = 0;
  let maxP = -1;
  for (let i = 0; i < 4; i++) {
    if (probs[i] > maxP) {
      maxP = probs[i];
      maxIdx = i;
    }
    const pct = (probs[i] * 100).toFixed(1);
    const pctEl = document.getElementById(`pct${i}`);
    const fillEl = document.getElementById(`fill${i}`);
    const itemEl = document.getElementById(`probItem${i}`);
    if (pctEl) pctEl.textContent = `${pct}%`;
    if (fillEl) fillEl.style.width = `${pct}%`;
    if (itemEl) itemEl.classList.remove('winner');
  }
  const winnerEl = document.getElementById(`probItem${maxIdx}`);
  if (winnerEl) winnerEl.classList.add('winner');

  // 6. Telemetry 15 Features (3 Kênh)
  const feats = data.latest.features || [];
  renderChannelTelemetry('ch1Metrics', feats, 0);
  renderChannelTelemetry('ch2Metrics', feats, 5);
  renderChannelTelemetry('ch3Metrics', feats, 10);

  // 7. Cột 1: Danh sách Thiết bị
  const devContainer = document.getElementById('deviceListContainer');
  const fleetBadge = document.getElementById('fleetBadge');
  const devCount = (data.devices || []).length;
  if (fleetBadge) fleetBadge.textContent = `${devCount} thiết bị`;

  if (devContainer) {
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
  }

  // 8. Cột 1: Đồng bộ Form Config lần đầu
  if (!isConfigSynced && data.config) {
    isConfigSynced = true;
    const confInput = document.getElementById('confInput');
    const confDisplay = document.getElementById('confDisplay');
    const voteInput = document.getElementById('voteInput');
    const voteDisplay = document.getElementById('voteDisplay');

    if (confInput) confInput.value = data.config.conf_threshold;
    if (confDisplay) confDisplay.textContent = data.config.conf_threshold.toFixed(2);
    if (voteInput) voteInput.value = data.config.vote_window;
    if (voteDisplay) voteDisplay.textContent = `${data.config.vote_window} gói`;

    if (data.config.actions) {
      for (let i = 0; i < 4; i++) {
        const actInput = document.getElementById(`act${i}`);
        if (actInput) actInput.value = data.config.actions[i] || '';
      }
    }
  }

  // 9. Cột 3: Nhật Ký Gói Tin (Log Stream Feed)
  const logContainer = document.getElementById('logStreamContainer');
  if (logContainer) {
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
  }
}

// Fetch State Fallback qua REST
async function fetchServerState() {
  try {
    const res = await fetch(`${API_BASE}/api/state`);
    if (!res.ok) throw new Error('API offline');
    const data = await res.json();
    renderAll(data);
  } catch (err) {
    const engineInfo = document.getElementById('engineInfo');
    if (engineInfo) engineInfo.textContent = 'Mất kết nối với C Server (Hãy chạy .\\run_server.bat)';
    const mqttChip = document.getElementById('mqttChip');
    if (mqttChip) mqttChip.className = 'status-chip';
    const mqttStatusText = document.getElementById('mqttStatusText');
    if (mqttStatusText) mqttStatusText.textContent = 'MQTT Broker: Disconnected';
  }
}

// Quản lý Chế độ Truyền nhận (SSE vs Polling)
function setTransportMode(mode) {
  const tag = document.getElementById('sseTransportTag');
  if (!tag) return;
  if (mode === 'sse') {
    tag.textContent = 'SSE Active (<2ms)';
    tag.className = 'version-tag sse-active';
  } else {
    tag.textContent = 'HTTP Poll (300ms)';
    tag.className = 'version-tag sse-polling';
  }
}

function startPolling() {
  if (pollingTimer) return;
  setTransportMode('poll');
  fetchServerState();
  pollingTimer = setInterval(fetchServerState, 300);
}

function stopPolling() {
  if (pollingTimer) {
    clearInterval(pollingTimer);
    pollingTimer = null;
  }
}

// Khởi tạo SSE Stream Kết Nối Trực Tiếp Sockets C Server
function initSSE() {
  if (typeof EventSource === 'undefined') {
    startPolling();
    return;
  }

  try {
    if (eventSource) {
      eventSource.close();
      eventSource = null;
    }

    eventSource = new EventSource(`${API_BASE}/api/stream`);

    eventSource.onopen = () => {
      stopPolling();
      setTransportMode('sse');
    };

    eventSource.onmessage = (event) => {
      try {
        const data = JSON.parse(event.data);
        renderAll(data);
      } catch (err) {
        console.error('Error parsing SSE JSON:', err);
      }
    };

    eventSource.onerror = () => {
      if (eventSource) {
        eventSource.close();
        eventSource = null;
      }
      startPolling();
      // Thử kết nối lại SSE sau 3 giây
      setTimeout(initSSE, 3000);
    };
  } catch (err) {
    startPolling();
  }
}

// Bắt Đầu / Reset Hiệu Chuẩn Người Dùng
async function startCalibration() {
  try {
    await fetch(`${API_BASE}/api/calibrate/start`, { method: 'POST' });
  } catch (err) {
    console.warn('Calibration start error', err);
  }
}

async function resetCalibration() {
  try {
    await fetch(`${API_BASE}/api/calibrate/reset`, { method: 'POST' });
  } catch (err) {
    console.warn('Calibration reset error', err);
  }
}

// Giả lập Phản hồi ACK từ End Device (Robotic Arm / Laptop App)
async function simulateEndDeviceAck() {
  try {
    await fetch(`${API_BASE}/api/end_device/ack`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        device_id: 'end_mcu_arm_01',
        cmd_id: currentCmdId,
        exec_time_ms: 42,
        status: 'COMPLETED'
      })
    });
  } catch (err) {
    console.warn('ACK simulation error', err);
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
  try {
    await fetch(`${API_BASE}/api/simulate`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
  } catch (err) {
    console.warn('Simulation error', err);
  }
}

// Thêm End Device mới
async function registerEndDevice() {
  const input = document.getElementById('newEndDevId');
  const devId = (input && input.value.trim()) || 'arm_robot_02';
  try {
    await fetch(`${API_BASE}/api/end_device`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ id: devId, status: 'ONLINE_READY' })
    });
    if (input) input.value = '';
  } catch (err) {
    console.warn('Device register error', err);
  }
}

// Khởi chạy SSE Stream ngay khi load trang
initSSE();
