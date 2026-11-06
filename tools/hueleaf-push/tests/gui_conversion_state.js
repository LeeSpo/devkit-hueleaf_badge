// Run the actual inline page script with controlled asynchronous HTTP replies.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const script = require('node:fs').readFileSync(0, 'utf8');
const elements = new Map(), requests = [], readers = [], images = [], uploads = [];
const httpRequests = [], probes = [], stripRequests = [], infoReplies = [];
let finishUpload, confirmed = false, rebootResult = {ok: true}, clock = 1000000;
let info = {fw: '1.4.0', battery: 80, free_kb: 1024, ready: true, strip: true, upload_no_refresh: true, uptime_ms: 100000};
let availablePorts = {ports: [], default: null};
let holdInfo = false, infoError = null;
function element() {
    return {value: '', style: {}, dataset: {}, disabled: true, checked: false, options: [],
        classList: {add() {}, remove() {}, contains() {return false;}},
        set innerHTML(v) {this.options = [];}, querySelectorAll() {return [];},
        addEventListener() {}, appendChild(v) {this.options.push(v);}, setPointerCapture() {},
        getContext() {return {translate() {}, rotate() {}, drawImage() {}};},
        toDataURL() {return 'data:rotated';}, clientWidth: 480};
}
const node = id => {
    if (!elements.has(id)) elements.set(id, element());
    return elements.get(id);
};
const context = vm.createContext({
    document: {getElementById: node, createElement: element, addEventListener() {}},
    window: {addEventListener() {}}, TextEncoder, console, clearTimeout,
    Date: class extends Date {static now() {return clock;}},
    confirm: () => confirmed,
    setTimeout(fn, delay) {if (delay === 2000) {clock += delay; return setImmediate(fn);} return setTimeout(fn, delay);},
    setInterval() {}, devicePixelRatio: 1,
    FileReader: class {readAsDataURL(file) {this.file = file; readers.push(this);}},
    Image: class {
        constructor() {this.naturalWidth = 480; this.naturalHeight = 720;}
        set src(v) {this.value = v; images.push(this);}
    },
    fetch: async (path, options) => {
        httpRequests.push({path, body: options && options.body && JSON.parse(options.body)});
        let body, ok = true;
        if (path === '/api/convert') {
            body = await new Promise((resolve, reject) => requests.push({
                body: JSON.parse(options.body), resolve, reject}));
        } else if (path === '/api/ports') body = availablePorts;
        else if (path === '/api/settings') body = {settings: {}, panel_mm: [52.1,78.2]};
        else if (path === '/api/send') {uploads.push(JSON.parse(options.body)); body = {job: 'upload'};}
        else if (path === '/api/job?id=upload') body = await new Promise(resolve => {finishUpload = resolve;});
        else if (path.startsWith('/api/list')) body = {connected: true, items: []};
        else if (path.startsWith('/api/info')) {
            if (infoError) throw infoError;
            body = holdInfo ? await new Promise(resolve => infoReplies.push(resolve)) :
                path.includes('probe=1') && probes.length ? probes.shift() : {...info, uptime_ms: info.uptime_ms + clock - 1000000};
            if (body instanceof Error) throw body;
        }
        else if (path === '/api/strip') {
            const request = JSON.parse(options.body);
            body = await new Promise(resolve => stripRequests.push({body: request, resolve}));
            ok = body.ok !== false;
            if (ok) info = {...info, strip: request.on};
        }
        else if (path === '/api/reboot') body = rebootResult;
        else throw new Error('Unexpected request: ' + path);
        return {ok, json: async () => body};
    }
});
const run = text => vm.runInContext(text, context);
const tick = () => new Promise(resolve => setImmediate(resolve));
const result = id => ({id, preview: id, name: id, coverage: 1});

(async () => {
    run(script);
    await tick();
    assert.equal(node('fullimage').disabled, true);
    assert.equal(node('fullimage').indeterminate, true);
    node('port').value = 'FAKE';
    await run('refresh()');
    assert.equal(node('fullimage').disabled, false);
    assert.equal(node('fullimage').checked, false);
    info = {...info, strip: false};
    await run('refresh()');
    assert.equal(node('fullimage').checked, true, 'connecting reads the saved setting');
    assert.equal(stripRequests.length, 0, 'connecting never changes the device setting');

    // A response from an older refresh cannot overwrite a newer device state.
    holdInfo = true;
    const staleRefresh = run('refresh()'); await tick();
    holdInfo = false;
    await run('refresh()');
    infoReplies.shift()({...info, strip: true});
    await staleRefresh;
    assert.equal(node('fullimage').checked, true);

    info = {...info, strip: true}; await run('refresh()');
    run("cur='existing-preview';orderDirty=true;syncControls()");
    node('fullimage').checked = true;
    const hideStrip = node('fullimage').onchange(); await tick();
    assert.deepEqual(stripRequests[0].body, {port: 'FAKE', on: false});
    for (const id of ['fullimage', 'send', 'reboot', 'port', 'rescan', 'refresh', 'saveorder']) {
        assert.equal(node(id).disabled, true, `${id} is disabled while changing display mode`);
    }
    await node('fullimage').onchange();
    await node('send').onclick();
    await node('reboot').onclick();
    assert.equal(stripRequests.length, 1);
    assert.equal(uploads.length, 0);
    stripRequests[0].resolve({ok: true}); await hideStrip;
    assert.equal(node('fullimage').checked, true);
    assert.equal(node('fullimage').disabled, false);
    assert.equal(run('cur'), 'existing-preview', 'changing display mode preserves the prepared image');
    assert.equal(run('orderDirty'), true, 'changing display mode preserves unsaved ordering');
    assert.equal(requests.length, 0, 'changing display mode does not reconvert the image');

    node('fullimage').checked = false;
    const showStrip = node('fullimage').onchange(); await tick();
    assert.deepEqual(stripRequests[1].body, {port: 'FAKE', on: true});
    stripRequests[1].resolve({ok: true}); await showStrip;
    assert.equal(node('fullimage').checked, false);

    node('fullimage').checked = true;
    const failedStrip = node('fullimage').onchange(); await tick();
    stripRequests[2].resolve({ok: false, error: '设置失败'}); await failedStrip;
    assert.equal(node('fullimage').checked, false, 'failed setting is restored from INFO');
    assert.equal(node('fullimage').disabled, false);
    assert.equal(node('msg').textContent, '设置失败');

    // A lost reply may follow a successful device change: INFO is authoritative.
    node('fullimage').checked = true;
    const lostStripAck = node('fullimage').onchange(); await tick();
    info = {...info, strip: false};
    stripRequests[3].resolve({ok: false, error: '未收到回复'}); await lostStripAck;
    assert.equal(node('fullimage').checked, true);

    node('fullimage').checked = false;
    const unpluggedStrip = node('fullimage').onchange(); await tick();
    infoError = new Error('USB gone');
    stripRequests[4].resolve({ok: false, error: '设备已断开'}); await unpluggedStrip;
    assert.equal(node('fullimage').disabled, true);
    assert.equal(node('fullimage').indeterminate, true);
    infoError = null; await run('refresh()');
    assert.equal(node('fullimage').checked, true);
    assert.equal(node('fullimage').disabled, false);

    info = {...info, ready: false}; await run('refresh()');
    assert.equal(node('fullimage').disabled, true);
    info = {...info, ready: true, strip: undefined}; await run('refresh()');
    assert.equal(node('fullimage').disabled, true);
    assert.equal(node('fullimage').indeterminate, true);
    await node('fullimage').onchange();
    assert.equal(stripRequests.length, 5, 'unknown device state must not be changed');
    info = {...info, strip: true}; await run('refresh()');
    node('mode').value = 'photo';
    run("src='data:first';fname='first.png'");
    const first = run('convert()');
    await tick();
    node('mode').value = 'fine'; run('convert()');
    node('mode').value = 'illustration'; run('convert()');
    assert.equal(requests.length, 1, 'coalesce changes while an expensive conversion runs');
    assert.equal(node('send').disabled, true);
    requests[0].resolve(result('stale-photo'));
    await first; await tick();
    assert.equal(requests.length, 2);
    assert.equal(requests[1].body.mode, 'illustration', 'next request uses latest mode');
    assert.equal(run('cur'), null, 'stale result cannot become sendable');
    requests[1].resolve(result('latest-flat'));
    await tick(); await tick();
    assert.equal(run('cur'), 'latest-flat');
    assert.equal(node('prev').src, 'latest-flat');
    assert.equal(node('send').disabled, false);

    node('mode').value = 'fine';
    const failed = run('convert()'); await tick();
    requests[2].reject(new Error('conversion failed'));
    await failed;
    assert.equal(run('cur'), null);
    assert.equal(node('send').disabled, true, 'failed conversion cannot send an older frame');

    // Editing invalidates an in-flight result immediately, before debounce.
    const editing = run('convert()'); await tick();
    run('changed()');
    requests[3].resolve(result('old-placement'));
    await editing;
    run('clearTimeout(ctimer)');
    assert.equal(run('cur'), null);
    assert.equal(node('send').disabled, true);

    // An earlier asynchronous file load must not replace a newly selected file.
    run("load({name:'old.png',type:'image/png'});load({name:'new.png',type:'image/png'})");
    readers[0].result = 'data:old'; await readers[0].onload();
    assert.equal(images.length, 0);
    readers[1].result = 'data:new'; await readers[1].onload();
    images[0].onload(); await tick();
    assert.equal(requests[4].body.image, 'data:new');
    requests[4].resolve(result('new-file')); await tick(); await tick();
    assert.equal(run('cur'), 'new-file');
    assert.equal(node('send').disabled, false);

    node('port').value = 'FAKE';
    const upload = node('send').onclick(); await tick();
    assert.equal(uploads[0].id, 'new-file');
    assert.equal(uploads[0].show, false);
    assert.equal(node('reboot').disabled, true);
    assert.equal(node('fullimage').disabled, true);
    await node('fullimage').onchange();
    assert.equal(stripRequests.length, 5, 'uploading cannot change display mode');
    await node('reboot').onclick();
    assert.equal(httpRequests.filter(x => x.path === '/api/reboot').length, 0);
    const duringUpload = run('convert()'); await tick();
    requests[5].resolve(result('new-preview-during-upload')); await duringUpload;
    assert.equal(node('send').disabled, true, 'converting cannot enable a second concurrent upload');
    finishUpload({state: 'ok', done: 1, total: 1}); await upload;
    assert.equal(run('cur'), 'new-preview-during-upload');
    assert.equal(node('send').disabled, false, 'latest valid preview remains sendable after upload');
    assert.equal(node('pct').textContent, '✓ 已保存，可继续发送下一张');

    // Three independent images can be sent without displaying or rebooting.
    for (let n = 2; n <= 3; ++n) {
        run(`cur='image-${n}';syncControls()`);
        const next = node('send').onclick(); await tick();
        finishUpload({state: 'ok', done: 1, total: 1}); await next;
        assert.equal(node('send').disabled, false);
    }
    assert.equal(uploads.length, 3);
    assert(uploads.every(x => x.show === false));
    assert.equal(httpRequests.filter(x => x.path === '/api/reboot').length, 0);

    // Cancelling confirmation leaves the device untouched.
    await node('reboot').onclick();
    assert.equal(httpRequests.filter(x => x.path === '/api/reboot').length, 0);
    confirmed = true;
    availablePorts = {ports: [{device: 'FAKE', hueleaf: true}], default: 'FAKE'};
    probes.push(new Error('USB gone'), {...info, ready: false, uptime_ms: 1000}, {...info, uptime_ms: 3000});
    const reboot = node('reboot').onclick();
    assert.equal(node('reboot').disabled, true);
    assert.equal(node('send').disabled, true);
    assert.equal(node('fullimage').disabled, true);
    await reboot;
    assert.equal(node('pct').textContent, '✓ 已重启并重新连接');
    assert.equal(httpRequests.filter(x => x.path === '/api/reboot').length, 1);
    assert.equal(node('send').disabled, false);
    assert.equal(node('fullimage').checked, false, 'reconnecting reads the saved setting');
    assert.equal(stripRequests.length, 5, 'restarting never writes the display setting');

    // A missing ACK with unchanged uptime must not be declared a restart or
    // cause a second REBOOT, even if INFO is immediately ready again.
    rebootResult = {ok: false, uncertain: true};
    await node('reboot').onclick();
    assert.equal(node('pct').textContent, '重启结果未确认');
    assert(node('msg').textContent.includes('重新检测'));
    assert.equal(httpRequests.filter(x => x.path === '/api/reboot').length, 2);

    // If uptime confirms the reset, a lost ACK can still reconnect safely.
    probes.push({...info, uptime_ms: 1000});
    await node('reboot').onclick();
    assert.equal(node('pct').textContent, '✓ 已重启并重新连接');
    assert.equal(httpRequests.filter(x => x.path === '/api/reboot').length, 3);

    info = {...info, upload_no_refresh: false};
    await run('refresh()');
    assert.equal(node('send').disabled, true, 'old firmware cannot silently refresh during an upload');
    assert(node('msg').textContent.includes('升级'));
    await node('send').onclick();
    assert.equal(uploads.length, 3);
})().catch(error => {console.error(error); process.exitCode = 1;});
