// Run the actual inline page script with controlled asynchronous HTTP replies.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const script = require('node:fs').readFileSync(0, 'utf8');
const elements = new Map(), requests = [], readers = [], images = [], uploads = [];
const httpRequests = [], probes = [], stripRequests = [], infoReplies = [];
const slideshowRequests = [];
let finishUpload, confirmed = false, sessionFailure = null, clock = 1000000;
const intervals = [], pollTimers = [];
let info = {fw: '1.5.0', battery: 80, free_kb: 1024, ready: true, state: 'idle', strip: true, upload_session_supported: true, upload_session_active: false, upload_no_refresh: true, uptime_ms: 100000};
let availablePorts = {ports: [], default: null};
let holdInfo = false, infoError = null, listBusy = false;
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
    setTimeout(fn, delay) {if (delay === 1000) {pollTimers.push(fn); return 0;} if (delay === 2000) {clock += delay; return setImmediate(fn);} return setTimeout(fn, delay);},
    setInterval(fn, delay) {intervals.push({fn, delay});}, devicePixelRatio: 1,
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
        else if (path === '/api/send') {uploads.push(JSON.parse(options.body)); info = {...info, upload_session_active: true}; body = {job: 'upload'};}
        else if (path === '/api/job?id=upload') body = await new Promise(resolve => {finishUpload = resolve;});
        else if (path.startsWith('/api/list')) {if(listBusy){ok=false;body={error:'设备忙碌',device_status:-9};}else body = {connected: true, items: []};}
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
        else if (path === '/api/slideshow') {
            const request = JSON.parse(options.body);
            body = await new Promise(resolve => slideshowRequests.push({body: request, resolve}));
            ok = body.ok !== false;
            if (ok) info = {...info, slideshow_enabled: request.enabled, slideshow_interval_min: request.interval_min, ...('tap_wake' in request ? {tap_wake: request.tap_wake} : {})};
        }
        else if (path === '/api/session') {
            const action = JSON.parse(options.body).action;
            if (sessionFailure) {ok = false; body = {error: sessionFailure};}
            else {if (action === 0) info = {...info, upload_session_active: false};
                body = {active: info.upload_session_active, supported: info.upload_session_supported};}
        }
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
    for (const id of ['fullimage', 'send', 'finish', 'port', 'rescan', 'refresh', 'saveorder']) {
        assert.equal(node(id).disabled, true, `${id} is disabled while changing display mode`);
    }
    await node('fullimage').onchange();
    await node('send').onclick();
    await node('finish').onclick();
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
    assert.equal(node('finish').disabled, true);
    assert.equal(node('fullimage').disabled, true);
    await node('fullimage').onchange();
    assert.equal(stripRequests.length, 5, 'uploading cannot change display mode');
    await node('finish').onclick();
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

    // Finishing only releases the session. A lost reply is safe to retry.
    assert.equal(node('finish').disabled, false);
    assert.equal(node('fullimage').disabled, true);
    assert.equal(node('port').disabled, true);
    assert(intervals.some(x => x.delay === 10000));
    await run('keepalive()');
    assert(httpRequests.some(x => x.path === '/api/session' && x.body.action === 2));
    sessionFailure = '未收到回复';
    await node('finish').onclick();
    assert.equal(run('batchActive'), true);
    assert.equal(node('finish').disabled, false);
    assert(node('msg').textContent.includes('结束传图未确认'));
    sessionFailure = null;
    await node('finish').onclick();
    assert.equal(run('batchActive'), false);
    assert.equal(node('finish').disabled, true);
    assert.equal(node('fullimage').disabled, false);
    assert.equal(node('port').disabled, false);
    assert.equal(node('pct').textContent, '✓ 传图已结束，画面保持原样');
    assert.equal(httpRequests.filter(x => x.path === '/api/reboot' || x.path === '/api/show').length, 0);
    assert.equal(uploads.length, 3, 'finishing does not upload the prepared preview again');

    // Firmware expiry stops heartbeats without starting another session.
    run('batchActive=true;syncControls()');
    await run('keepalive()');
    assert.equal(run('batchActive'), false);
    assert(node('msg').textContent.includes('会话已结束'));

    info = {...info, state: 'refreshing'};
    await run('refresh()');
    assert.equal(node('send').disabled, true);
    assert(pollTimers.length > 0);
    info = {...info, state: 'idle', upload_session_supported: false};
    await run('refresh()');
    assert(node('uploadhint').textContent.includes('整批按键保护'));
    assert.equal(node('send').disabled, false, 'old firmware retains quiet upload support');

    info = {...info, upload_session_supported: true};
    listBusy = true; await run('refresh()');
    assert.equal(run('connected'), true, 'a busy list reply is not a disconnection');
    assert.equal(node('send').disabled, true);
    assert(node('dev').textContent.includes('维护中'));
    listBusy = false; await run('refresh()');
    assert.equal(node('send').disabled, false);

    info = {...info, upload_no_refresh: false};
    await run('refresh()');
    assert.equal(node('send').disabled, true, 'old firmware cannot silently refresh during an upload');
    assert(node('msg').textContent.includes('升级'));
    await node('send').onclick();
    assert.equal(uploads.length, 3);

    // Decimal percentages use the fill-relative scale and preserve the centre.
    run('rw=960;rh=1440;sc=0.5;ox=0;oy=0;layout()');
    assert.equal(node('zpct').disabled, false);
    node('zpct').value = '100.5';
    node('zpct').onchange();
    run('clearTimeout(ctimer)');
    assert(Math.abs(run('sc') - 0.5025) < 1e-12);
    assert(Math.abs(run('(PW/2-ox)/sc') - 480) < 1e-10);
    assert(Math.abs(run('(PH/2-oy)/sc') - 720) < 1e-10);
    assert.equal(Number(node('zpct').value), 100.5);
    assert.equal(run('cur'), null, 'manual scaling invalidates the old preview');
    run('savePlace()');
    const placement = httpRequests.filter(x => x.path === '/api/settings' && x.body).at(-1);
    assert(Math.abs(Object.values(placement.body.placements)[0].scale - 0.5025) < 1e-12);
    const decimalConversion = run('convert()'); await tick();
    assert(Math.abs(requests.at(-1).body.place.scale - 0.5025) < 1e-12);
    requests.at(-1).resolve(result('decimal-scale')); await decimalConversion;

    for (const value of ['', 'invalid', '0', '-5']) {
        node('zpct').value = value;
        node('zpct').onchange();
        assert.equal(Number(node('zpct').value), 100.5, 'invalid input restores the current percentage');
        assert.equal(run('cur'), 'decimal-scale');
    }
    for (const [value, expected] of [['1', 10], ['2500', 2000]]) {
        node('zpct').value = value;
        node('zpct').onchange();
        run('clearTimeout(ctimer)');
        assert.equal(Number(node('zpct').value), expected, 'manual input respects zoom limits');
    }
    node('zs').value = '25'; node('zs').oninput(); run('clearTimeout(ctimer)');
    assert.equal(Number(node('zpct').value), 128.4, 'slider updates the decimal percentage');
    run('zoomAt(1.01,PW/2,PH/2);clearTimeout(ctimer)');
    assert.equal(Number(node('zpct').value), 129.69, 'wheel zoom updates the decimal percentage');
    node('bfill').onclick(); run('clearTimeout(ctimer)');
    assert.equal(Number(node('zpct').value), 100, 'presets update the percentage');

    // Timed playback negotiates capabilities and preserves drafts across polls.
    assert.equal(node('slideshow').disabled, true, 'older firmware is read-only');
    info = {...info, slideshow_supported: true, slideshow_enabled: false, slideshow_interval_min: 60, slideshow_state: 'off', upload_session_active: false};
    run('batchActive=false'); await run('refresh()');
    assert.equal(node('slideshow').disabled, false);
    assert.equal(Number(node('slideinterval').value), 60);
    assert.equal(slideshowRequests.length, 0, 'connecting never enables playback');
    node('slideshow').checked = true; node('slideshow').onchange();
    node('slideinterval').value = '30'; node('slideinterval').oninput();
    info = {...info, slideshow_interval_min: 120}; await run('refresh()');
    assert.equal(Number(node('slideinterval').value), 30, 'polls preserve unsaved interval');
    node('slideinterval').value = '1.5'; await node('saveslide').onclick();
    assert.equal(slideshowRequests.length, 0);
    node('slideinterval').value = '30';
    run("orderDirty=true;cur='prepared';syncControls()");
    const saveSlide = node('saveslide').onclick(); await tick();
    assert.deepEqual(slideshowRequests[0].body, {port: 'FAKE', enabled: true, interval_min: 30});
    for (const id of ['slideshow', 'slideinterval', 'saveslide', 'fullimage', 'send', 'finish', 'port', 'rescan', 'refresh', 'saveorder'])
        assert.equal(node(id).disabled, true, `${id} disabled while saving playback`);
    await node('saveslide').onclick(); assert.equal(slideshowRequests.length, 1);
    slideshowRequests[0].resolve({ok: true}); await saveSlide;
    assert.equal(node('slideshow').checked, true);
    assert.equal(Number(node('slideinterval').value), 30);
    assert.equal(run('orderDirty'), true);
    assert.equal(run('cur'), 'prepared');
    node('slideinterval').value = '90'; node('slideinterval').oninput();
    const badSlide = node('saveslide').onclick(); await tick();
    slideshowRequests[1].resolve({ok: false, error: 'disk write failed'}); await badSlide;
    assert.equal(Number(node('slideinterval').value), 90, 'failed writes preserve the draft');
    assert.equal(run('deviceInfo.slideshow_interval_min'), 30, 'failed writes do not change saved state');
    assert(node('msg').textContent.includes('disk write failed'));
    info = {...info, slideshow_supported: false}; await run('refresh()');
    assert.equal(node('saveslide').disabled, true);
    info = {...info, slideshow_supported: true, slideshow_interval_min: 15};
    node('port').value = 'SECOND'; await run('refresh()');
    assert.equal(Number(node('slideinterval').value), 15, 'drafts never leak into another device');
    assert.equal(node('tapwake').disabled, true, 'older playback firmware cannot receive extended payloads');
    info = {...info, tap_wake_supported: true, tap_wake: false}; await run('refresh()');
    assert.equal(node('tapwake').disabled, false);
    assert.equal(node('tapwake').checked, false, 'tap wake defaults off');
    node('tapwake').checked = true; node('tapwake').onchange();
    await run('refresh()');
    assert.equal(node('tapwake').checked, true, 'polls preserve unsaved wake option');
    const saveWake = node('saveslide').onclick(); await tick();
    assert.deepEqual(slideshowRequests[2].body, {port: 'SECOND', enabled: true, interval_min: 15, tap_wake: true});
    assert.equal(node('tapwake').disabled, true);
    slideshowRequests[2].resolve({ok: true}); await saveWake;
    assert.equal(run('deviceInfo.tap_wake'), true);
    run('batchActive=true;syncControls()');
    assert.equal(node('tapwake').disabled, true, 'settings remain frozen throughout upload session');
    run('batchActive=false');
    node('tapwake').checked = false; node('tapwake').onchange();
    const failWake = node('saveslide').onclick(); await tick();
    slideshowRequests[3].resolve({ok: false, error: 'write failed'}); await failWake;
    assert.equal(node('tapwake').checked, false, 'failed save keeps the draft');
    assert.equal(run('deviceInfo.tap_wake'), true, 'failed save keeps the device state');
})().catch(error => {console.error(error); process.exitCode = 1;});
