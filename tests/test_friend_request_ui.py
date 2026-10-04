"""Behavior checks for the friendship request controls in nearby.html."""

from pathlib import Path
import re
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parent.parent
NODE = shutil.which("node")
SCRIPT = next(
    body for body in re.findall(r"<script[^>]*>([\s\S]*?)</script>",
                                (ROOT / "WebUI" / "nearby.html").read_text(encoding="utf-8"))
    if "function card" in body
)


HARNESS = r'''
const assert = require('node:assert/strict');
const elements = {};
function element(id) { return elements[id] ||= { textContent:'', innerHTML:'', value:'', disabled:false }; }
global.document = {
  getElementById: element,
  createElement: () => ({ textContent:'', get innerHTML(){ return this.textContent; } }),
  querySelectorAll: () => []
};
global.localStorage = { getItem: () => null, setItem: () => {} };
global.navigator = {};
global.window = global;
global.setInterval = () => 0;
'''


class FriendRequestUiTests(unittest.TestCase):
    @unittest.skipUnless(NODE, "Node.js is needed for UI behavior checks")
    def test_friendship_states_and_post_actions(self):
        source = HARNESS + SCRIPT + r'''
;(async()=>{
globalThis.render = card;
function button(html) { return html.match(/<button[^>]*data-action="([^"]*)"[^>]*>([^<]*)/); }
let incoming = render({userId:1, username:'A', friendRequest:'incoming', requestExpiresIn:298}, true);
assert.equal(button(incoming)[1], 'accept');
assert.match(incoming, /Accept Request/);
assert.match(incoming, /Cancel request/);

let outgoing = render({userId:2, username:'B', friendRequest:'outgoing', requestExpiresIn:250}, true);
assert.match(outgoing, /Request pending/);
assert.match(outgoing, /Cancel request/);
assert.match(outgoing, /data-action="cancel"[^>]*>Cancel request/);
assert.match(outgoing, /data-action="cancel"[^>]*>/);

let pairing = render({userId:3, username:'C', friendRequest:'pairing'}, true);
assert.match(pairing, /Pairing\.\.\./);
assert.match(pairing, /data-action=""[^>]*disabled/);

let offline = render({userId:4, username:'D', friendRequest:'', friend:true, nearby:false}, false);
assert.match(offline, /Remove friend/);
let offlineRequest = render({userId:5, username:'E', friendRequest:'', nearby:false}, true);
assert.match(offlineRequest, /disabled/);
})();
'''
        result = subprocess.run([NODE, "-"], input=source, text=True,
                                capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless(NODE, "Node.js is needed for UI behavior checks")
    def test_busy_and_error_state_survive_refresh(self):
        source = HARNESS + SCRIPT + r'''
;(async()=>{
let postStarted;
global.fetch = (url, options) => {
  if (options) { postStarted = JSON.parse(options.body); return new Promise(resolve => { global.finishPost = () => resolve({ok:true}); }); }
  return Promise.resolve({json: () => Promise.resolve({enabled:true, timeValid:true, people:[]})});
};
const list = element('nearbyList');
const control = {dataset:{user:'7', action:'request'}, disabled:false};
document.querySelectorAll = () => [control];
bindFriendButtons();
control.onclick();
assert.equal(control.disabled, true);
assert.deepEqual(postStarted, {userId:7, action:'request'});
await refreshNearby();
assert.equal(control.disabled, true);
statusError = 'temporary failure'; statusErrorUntil = Date.now() + 5000;
await refreshNearby();
assert.equal(element('state').textContent, 'temporary failure');
})();
'''
        result = subprocess.run([NODE, "-"], input=source,
                                text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
