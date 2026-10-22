(() => {
  const list = document.getElementById('beams');
  const connection = document.getElementById('connection');
  const summary = document.getElementById('summary');
  const bridgePort = new URLSearchParams(location.search).get('rosbridge_port') || '9090';
  const rows = [];
  let socket;
  let reconnectTimer;
  let lastMessage = 0;
  let controllerHealthy = false;
  let closing = false;

  function makeRows(count) {
    if (rows.length === count) return;
    list.replaceChildren();
    rows.length = 0;
    for (let i = 0; i < count; i++) {
      const li = document.createElement('li');
      li.className = 'unknown';
      li.innerHTML = `<span class="i">${i + 1}</span><b>—</b>`;
      list.appendChild(li);
      rows.push(li);
    }
  }

  function unavailable(message) {
    controllerHealthy = false;
    connection.textContent = message;
    connection.dataset.state = 'unavailable';
    summary.textContent = 'Beam state unavailable';
    for (const li of rows) {
      li.querySelector('b').textContent = '—';
      li.classList.remove('on');
      li.classList.add('unknown');
    }
  }

  function render(msg) {
    const count = msg.beam_count;
    if (!Number.isInteger(count) || count < 1 || count > 240 || !Array.isArray(msg.beams)) {
      unavailable('Invalid light-curtain data');
      return;
    }
    makeRows(count);
    lastMessage = performance.now();
    const beams = new Map();
    for (const beam of msg.beams) {
      if (Number.isInteger(beam.index) && beam.index >= 0 && beam.index < count) {
        beams.set(beam.index, beam);
      }
    }
    let blocked = 0;
    let healthy = 0;
    for (let i = 0; i < count; i++) {
      const beam = beams.get(i);
      const valid = beam && beam.health === 0 && typeof beam.blocked === 'boolean';
      const value = valid && beam.blocked;
      const li = rows[i];
      li.querySelector('b').textContent = valid ? (value ? '1' : '0') : '—';
      li.classList.toggle('on', Boolean(value));
      li.classList.toggle('unknown', !valid);
      if (valid) healthy++;
      if (value) blocked++;
    }
    controllerHealthy = healthy === count;
    connection.textContent = controllerHealthy ? 'GC-1000 data live' : 'GC-1000 data stale or faulted';
    connection.dataset.state = controllerHealthy ? 'live' : 'unavailable';
    summary.textContent = controllerHealthy
      ? `Blocked beams: ${blocked} / ${count}`
      : `Valid beams: ${healthy} / ${count} · waiting for controller`;
  }

  function connect() {
    if (closing) return;
    const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
    socket = new WebSocket(`${protocol}://${location.hostname || 'localhost'}:${bridgePort}`);
    socket.onopen = () => {
      unavailable('ROS bridge connected · waiting for GC-1000');
      socket.send(JSON.stringify({
        op: 'subscribe', id: 'curtain-monitor', topic: '/gl_r/status',
        type: 'light_curtain/msg/CurtainStatus', throttle_rate: 100, queue_length: 1,
      }));
    };
    socket.onmessage = (event) => {
      try {
        const packet = JSON.parse(event.data);
        if (packet.op === 'publish' && packet.topic === '/gl_r/status') {
          render(packet.msg);
        } else if (packet.op === 'status' && packet.level === 'error') {
          unavailable('ROS subscription error · check the monitor log');
        }
      } catch {
        unavailable('Invalid message from ROS bridge');
      }
    };
    socket.onerror = () => unavailable('ROS bridge connection error');
    socket.onclose = () => {
      unavailable('ROS bridge disconnected · reconnecting');
      if (!closing) reconnectTimer = setTimeout(connect, 1000);
    };
  }

  makeRows(52);
  connect();
  setInterval(() => {
    if (controllerHealthy && performance.now() - lastMessage > 1000) {
      unavailable('No recent light-curtain data');
    }
  }, 250);
  window.addEventListener('beforeunload', () => {
    closing = true;
    clearTimeout(reconnectTimer);
    if (socket) socket.close();
  });
})();
