const API_BASE = window.location.protocol === 'file:'
  ? 'http://127.0.0.1:8080'
  : '';

const FEAT_NAMES = ['MAV', 'RMS', 'WL', 'ZC', 'SSC'];
let configSynced = false;
let simSeq = 100;

const SIM_PRESETS = {
  RESTING:   [0.0005, 0.0007, 0.08, 45, 78,  0.0006, 0.0008, 0.09, 44, 75,  0.0005, 0.0007, 0.08, 46, 80],
  FIST:      [0.0155, 0.0210, 2.35, 62, 95,  0.0052, 0.0070, 0.80, 52, 81,  0.0110, 0.0150, 1.75, 65, 93],
  OPEN_HAND: [0.0035, 0.0048, 0.55, 50, 82,  0.0145, 0.0195, 2.15, 58, 88,  0.0095, 0.0128, 1.45, 60, 89],
  POINTING:  [0.0028, 0.0039, 0.42, 72, 108, 0.0085, 0.0115, 1.32, 61, 90,  0.0165, 0.0225, 2.55, 84, 112]
};

function renderChannelFeatures(elementId, arr, offset) {
  const ul = document.getElementById(elementId);
  let html = '';
  for (let i = 0; i < 5; i++) {
    const val = arr[offset + i] !== undefined ? arr[offset + i] : 0;
    const formatted = (i < 3) ? val.toFixed(5) : val.toFixed(0);
    html += `<li><span>${FEAT_NAMES[i]}</span><strong>${formatted}</strong></li>`;
  }
  ul.innerHTML = html;
}

async function fetchServerState() {
  try {
    const res = await fetch(`${API_BASE}/api/state`);
    if (!res.ok) throw new Error('HTTP error');
    const data = await res.json();

    document.getElementById('srvDot').classList.add('online');
    document.getElementById('engineInfo').textContent = `Engine: ${data.engine} • API: Online`;

    const mqttBadge = document.getElementById('mqttBadge');
    if (data.mqtt_connected) {
      mqttBadge.textContent = 'MQTT Broker: Connected (1883)';
      mqttBadge.className = 'status-pill ok';
    } else {
      mqttBadge.textContent = 'MQTT Broker: Reconnecting...';
      mqttBadge.className = 'status-pill';
    }

    document.getElementById('uptimeBadge').textContent = `Uptime: ${data.uptime_sec}s`;
    document.getElementById('kpiEndDev').textContent = `${data.enddev_online} / ${data.enddev_total}`;
    document.getElementById('kpiSensors').textContent = `${data.sensor_online} / ${data.sensor_total}`;
    document.getElementById('kpiRx').textContent = data.total_rx;
    document.getElementById('kpiLastSensor').textContent = `Nguồn gần nhất: ${data.latest.sensor_id}`;
    document.getElementById('kpiGesture').textContent = data.latest.stable_label;
    document.getElementById('kpiTx').textContent = `Đã phát ${data.total_tx} lệnh điều khiển (Tx)`;

    document.getElementById('seqLabel').textContent = `seq #${data.latest.seq}`;

    for (let i = 0; i < 4; i++) {
      const pct = ((data.latest.probs[i] || 0) * 100).toFixed(1);
      document.getElementById(`bar${i}`).style.width = `${pct}%`;
      document.getElementById(`val${i}`).textContent = `${pct}%`;
    }

    const feats = data.latest.features || [];
    renderChannelFeatures('ch1List', feats, 0);
    renderChannelFeatures('ch2List', feats, 5);
    renderChannelFeatures('ch3List', feats, 10);

    if (!configSynced && data.config) {
      configSynced = true;
      document.getElementById('confInput').value = data.config.conf_threshold;
      document.getElementById('confDisplay').textContent = data.config.conf_threshold.toFixed(2);
      document.getElementById('voteInput').value = data.config.vote_window;
      document.getElementById('voteDisplay').textContent = data.config.vote_window;
      if (data.config.actions) {
        for (let i = 0; i < 4; i++) {
          document.getElementById(`act${i}`).value = data.config.actions[i] || '';
        }
      }
    }

    const devBody = document.getElementById('deviceTableBody');
    if (!data.devices || data.devices.length === 0) {
      devBody.innerHTML = '<tr><td colspan="6" class="empty-cell">Chưa có thiết bị nào kết nối</td></tr>';
    } else {
      devBody.innerHTML = data.devices.map(d => `
        <tr>
          <td><strong>${d.id}</strong></td>
          <td><span class="badge role">${d.role}</span></td>
          <td>
            <span class="badge ${d.online ? 'online' : 'offline'}">
              ${d.online ? 'ONLINE' : 'OFFLINE'}
            </span>
          </td>
          <td>${d.packets}</td>
          <td>${d.age_sec}s trước</td>
          <td><code>${d.status}</code></td>
        </tr>
      `).join('');
    }

    const logBody = document.getElementById('logTableBody');
    if (!data.logs || data.logs.length === 0) {
      logBody.innerHTML = '<tr><td colspan="6" class="empty-cell">Chưa có gói tin sEMG nào...</td></tr>';
    } else {
      logBody.innerHTML = data.logs.map(lg => `
        <tr>
          <td>${lg.time}</td>
          <td><code>${lg.device_id}</code></td>
          <td>#${lg.seq}</td>
          <td><strong>${lg.raw_label}</strong></td>
          <td>${(lg.conf * 100).toFixed(1)}%</td>
          <td><code>${lg.stable_label}</code></td>
        </tr>
      `).join('');
    }
  } catch (err) {
    document.getElementById('srvDot').classList.remove('online');
    document.getElementById('engineInfo').textContent = 'Mất kết nối với semg_server.exe (Hãy chạy .\\run_server.bat)';
  }
}

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

  const statusEl = document.getElementById('saveStatus');
  try {
    const res = await fetch(`${API_BASE}/api/config`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    if (res.ok) {
      statusEl.textContent = '✓ Đã cập nhật trực tiếp vào bộ nhớ Server C!';
      setTimeout(() => { statusEl.textContent = ''; }, 3000);
      fetchServerState();
    }
  } catch (err) {
    statusEl.textContent = '✗ Không kết nối được Server C!';
  }
}

async function sendSimGesture(presetName) {
  const f = SIM_PRESETS[presetName] || SIM_PRESETS.RESTING;
  simSeq++;
  const payload = {
    device_id: 'esp32_sim_gui',
    seq: simSeq,
    f: f
  };
  await fetch(`${API_BASE}/api/simulate`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  fetchServerState();
}

async function registerEndDevice() {
  const input = document.getElementById('newEndDevId');
  const devId = input.value.trim() || 'end_device_02';
  await fetch(`${API_BASE}/api/end_device`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ id: devId, status: 'ONLINE_READY' })
  });
  input.value = '';
  fetchServerState();
}

fetchServerState();
setInterval(fetchServerState, 400);
