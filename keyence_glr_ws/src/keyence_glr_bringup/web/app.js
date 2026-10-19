(() => {
  const N = 52;
  const list = document.getElementById('beams');
  const rows = [];
  for (let i = 0; i < N; i++) {
    const li = document.createElement('li');
    li.innerHTML = `<span class="i">${i + 1}</span><b>0</b>`;
    list.appendChild(li);
    rows.push(li);
  }

  const ros = new ROSLIB.Ros({ url: `ws://${location.hostname || 'localhost'}:9090` });
  new ROSLIB.Topic({
    ros,
    name: '/gl_r/status',
    messageType: 'keyence_glr_msgs/msg/CurtainStatus',
  }).subscribe((msg) => {
    for (const b of msg.beams) {
      const li = rows[b.index];
      if (!li) continue;
      const v = b.blocked ? 1 : 0;
      li.querySelector('b').textContent = v;
      li.classList.toggle('on', v === 1);
    }
  });
})();
