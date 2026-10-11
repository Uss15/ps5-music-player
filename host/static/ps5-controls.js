(() => {
  const $ = id => document.getElementById(id);
  const host = $('ps5Host'), port = $('ps5Port');
  const buttons = Array.from(document.querySelectorAll('.control'));
  const previous = buttons[0], toggle = buttons[1], next = buttons[2];
  const volume = document.querySelector('.volume input');
  const hint = document.querySelector('.control-hint');
  const progress = document.querySelector('.progress span');
  const progressLabels = document.querySelectorAll('.progress-wrap > span');
  let available = false, currentState = 'UNAVAILABLE', currentTrack = 0;

  function time(ms) {
    const seconds = Math.floor((Number(ms) || 0) / 1000);
    return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, '0')}`;
  }
  function setEnabled(value) {
    for (const button of buttons) {
      button.disabled = !value;
      button.title = value ? ['Previous track', 'Play or resume', 'Next track'][buttons.indexOf(button)] : 'Connect a PS5 with a working AudioOut backend first';
      button.style.cursor = value ? 'pointer' : 'not-allowed';
      button.style.color = value ? (button === toggle ? '#111911' : '#dbe5f1') : '';
      if (button === toggle && value) button.style.background = '#b6ff63';
    }
    if (volume) {
      volume.disabled = !value;
      volume.title = value ? 'Set PS5 playback volume' : 'Volume control requires a working PS5 AudioOut backend';
    }
  }
  function paint(data) {
    available = Boolean(data.available);
    currentState = data.state || 'UNAVAILABLE';
    currentTrack = Number(data.track) || 0;
    setEnabled(available && document.querySelectorAll('.track').length > 0);
    if (hint) hint.textContent = available
      ? `${currentState} · BGM output selected; confirm game-audio mixing on your PS5`
      : `PS5 AudioOut unavailable${data.error ? `: ${data.error}` : '. Start a compatible payload.'}`;
    const notice = document.querySelector('.notice > div');
    if (notice) notice.innerHTML = available
      ? '<strong>PS5 AudioOut is ready.</strong> The payload selected the BGM output route; verify that it mixes with game audio on your console.'
      : '<strong>PS5 AudioOut is not ready.</strong> Connect to a compatible payload to check the audio route and enable PS5 controls.';
    const sidebar = document.querySelector('.sidebar-note');
    if (sidebar) {
      sidebar.querySelector('strong').innerHTML = `<span class="dot"></span>${available ? 'PS5 AudioOut ready' : 'PS5 audio status pending'}`;
      sidebar.querySelector('p').textContent = available
        ? 'MP3/WAV playback controls are connected. Confirm game-audio mixing on the target console.'
        : 'Connect and scan a compatible PS5 payload to check its audio output status.';
    }
    if (volume && document.activeElement !== volume && Number.isFinite(Number(data.volume))) volume.value = String(data.volume);
    if (progress) progress.style.width = `${data.durationMs ? Math.min(100, 100 * data.positionMs / data.durationMs) : 0}%`;
    if (progressLabels.length >= 2) {
      progressLabels[0].textContent = time(data.positionMs);
      progressLabels[1].textContent = time(data.durationMs);
    }
    if (toggle) {
      toggle.title = currentState === 'PLAYING' ? 'Pause playback' : 'Play or resume';
      toggle.setAttribute('aria-label', toggle.title);
      toggle.innerHTML = currentState === 'PLAYING'
        ? '<svg viewBox="0 0 24 24"><path d="M8 5v14M16 5v14"/></svg>'
        : '<svg viewBox="0 0 24 24"><path d="M9 5v14l11-7z"/></svg>';
    }
    document.querySelectorAll('.track').forEach(row => {
      const number = Number(row.querySelector('.num')?.textContent);
      row.classList.toggle('selected', number === currentTrack && currentTrack > 0);
    });
    if (currentTrack > 0) {
      const row = Array.from(document.querySelectorAll('.track')).find(item => Number(item.querySelector('.num')?.textContent) === currentTrack);
      const title = row?.querySelector('.track-name strong')?.textContent;
      if (title) {
        $('nowTitle').textContent = title;
        $('nowSubtitle').textContent = `${currentState} · PS5 BGM output`;
      }
    }
  }
  async function readStatus() {
    const address = host?.value.trim();
    if (!address || !$('connectionPill')?.classList.contains('online')) return;
    const query = new URLSearchParams({host: address, port: port?.value.trim() || '9023'});
    try {
      const response = await fetch(`/api/status?${query}`, {cache: 'no-store'});
      const data = await response.json();
      if (response.ok) paint(data);
      else paint({available: false, error: data.error});
    } catch (_) { /* transient polling/network error; keep the last known UI state */ }
  }
  async function control(command, extra = {}) {
    if (!available) return;
    const response = await fetch('/api/control', {
      method: 'POST', headers: {'Content-Type': 'application/json'},
      body: JSON.stringify({host: host.value.trim(), port: Number(port.value || 9023), command, ...extra})
    });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || 'Control command failed.');
    window.setTimeout(readStatus, 180);
  }
  async function withFeedback(command, extra) {
    try { await control(command, extra); }
    catch (error) { if (hint) hint.textContent = error.message; }
  }
  previous?.addEventListener('click', () => withFeedback('previous'));
  next?.addEventListener('click', () => withFeedback('next'));
  toggle?.addEventListener('click', () => {
    if (currentState === 'PLAYING') return withFeedback('pause');
    if (currentState === 'PAUSED') return withFeedback('resume');
    const selected = Number(document.querySelector('.track.selected .num')?.textContent) || currentTrack || 1;
    return withFeedback('play', {track: selected});
  });
  volume?.addEventListener('change', () => withFeedback('volume', {volume: Math.round(Number(volume.value))}));
  $('trackList')?.addEventListener('click', event => {
    const row = event.target.closest('.track');
    if (!row || !available) return;
    const number = Number(row.querySelector('.num')?.textContent);
    if (number > 0) withFeedback('play', {track: number});
  });
  $('trackList')?.addEventListener('keydown', event => {
    if (event.key !== 'Enter' && event.key !== ' ') return;
    const row = event.target.closest('.track');
    if (!row || !available) return;
    const number = Number(row.querySelector('.num')?.textContent);
    if (number > 0) withFeedback('play', {track: number});
  });
  $('connectBtn')?.addEventListener('click', () => window.setTimeout(readStatus, 500));
  $('refreshBtn')?.addEventListener('click', () => window.setTimeout(readStatus, 500));
  window.setInterval(readStatus, 1500);
  paint({available: false, error: 'Connect & scan to check the PS5 audio output.'});
})();
