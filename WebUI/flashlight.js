(() => {
  let state = {
    flashlight: false,
    emergency: false,
    remoteEmergency: false,
    remoteEmergencyUsername: '',
    remoteEmergencyFaction: false,
    brightness: 60,
    temperature: 50
  };
  let timer;
  const byId = id => document.getElementById(id);

  function createUi() {
    const nav = document.querySelector('nav');
    if (!nav || byId('flashIcon')) return false;
    nav.insertAdjacentHTML('beforeend',
      '<button id="flashIcon" class="flash-button" type="button" aria-label="Flashlight controls" aria-expanded="false"><svg viewBox="0 0 32 32" aria-hidden="true"><path d="M10 3h12l-2 8v4l5 5v9H7v-9l5-5v-4z"/></svg></button>'
      );
    nav.insertAdjacentHTML('afterend',
      '<aside id="flashPanel" class="flash-panel" hidden><div class="flash-head"><strong>Flashlight</strong><button id="flashPower" type="button">Turn on</button></div><label>Brightness <output id="flashBrightnessValue">60%</output><input id="flashBrightness" type="range" min="0" max="100" value="60"></label><label>Color temperature <output id="flashTemperatureValue">Neutral</output><input id="flashTemperature" type="range" min="0" max="100" value="50"></label><div class="temperature-labels"><span>Cool</span><span>NW only</span><span>Warm</span></div><p id="flashMessage" class="muted"></p></aside>'
      );
    nav.insertAdjacentHTML('afterend',
      '<aside id="remoteEmergencyAlert" class="remote-emergency-alert" role="alert" hidden>Agent <strong id="remoteEmergencyAgent"></strong> has an emergency</aside>'
      );
    return true;
  }

  function updateUi() {
    byId('flashBrightness').value = state.brightness;
    byId('flashTemperature').value = state.temperature;
    byId('flashBrightnessValue').textContent = state.brightness + '%';
    byId('flashTemperatureValue').textContent = state.temperature === 50 ? 'Neutral' : state
      .temperature < 50 ? `Cool ${50 - state.temperature}` : `Warm ${state.temperature - 50}`;
    byId('flashPower').textContent = state.flashlight ? 'Turn off' : 'Turn on';
    byId('flashPower').disabled = state.emergency;
    byId('flashIcon').classList.toggle('active', state.flashlight);
    byId('flashIcon').classList.toggle('emergency', state.emergency);
    const alert = byId('remoteEmergencyAlert');
    alert.hidden = !state.remoteEmergency;
    if (state.remoteEmergency) {
      const agent = byId('remoteEmergencyAgent');
      agent.textContent = state.remoteEmergencyUsername || 'Unknown Agent';
      agent.className = state.remoteEmergencyFaction ? 'agent-enl' : 'agent-res';
    }
  }

  async function refresh() {
    try {
      const response = await fetch('/api/misc');
      if (!response.ok) throw Error();
      state = await response.json();
      updateUi();
    } catch (_) {}
  }

  async function send(enabled = state.flashlight) {
    const body = {
      enabled,
      brightness: +byId('flashBrightness').value,
      temperature: +byId('flashTemperature').value
    };
    try {
      const response = await fetch('/api/flashlight', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        },
        body: JSON.stringify(body)
      });
      if (!response.ok) throw Error(await response.text());
      await refresh();
    } catch (error) {
      byId('flashMessage').textContent = error.message || 'Flashlight update failed.';
    }
  }

  function init() {
    if (!createUi()) return;
    byId('flashIcon').onclick = async () => {
      const open = byId('flashPanel').hidden;
      byId('flashPanel').hidden = !open;
      byId('flashIcon').setAttribute('aria-expanded', open);
      if (open) await refresh();
    };
    byId('flashPower').onclick = () => send(!state.flashlight);
    for (const id of ['flashBrightness', 'flashTemperature']) {
      byId(id).oninput = () => {
        state.brightness = +byId('flashBrightness').value;
        state.temperature = +byId('flashTemperature').value;
        updateUi();
        clearTimeout(timer);
        timer = setTimeout(() => send(), 100);
      };
    }
    refresh();
    setInterval(refresh, 2000);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
