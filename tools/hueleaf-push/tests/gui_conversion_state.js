// Run web/app.js with controlled DOM events and asynchronous HTTP replies.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const script = require('node:fs').readFileSync(0, 'utf8');
const elements = new Map(), requests = [], readers = [], images = [], uploads = [];
const httpRequests = [], probes = [], stripRequests = [], infoReplies = [];
const slideshowRequests = [], selectionRequests = [], windowHandlers = {}, documentHandlers = {};
let selectionListing = {connected: true, items: []};
let finishUpload, confirmed = false, sessionFailure = null, clock = 1000000;
const intervals = [], pollTimers = [];
let info = {fw: '1.5.0', battery: 80, free_kb: 1024, ready: true, state: 'idle', strip: true, upload_session_supported: true, upload_session_active: false, upload_no_refresh: true, uptime_ms: 100000};
let availablePorts = {ports: [], default: null};
let holdInfo = false, infoError = null, listBusy = false;
function element(tag = 'div') {
    return {tag, value: '', style: {}, dataset: {}, disabled: true, checked: false, options: [], children: [], handlers: {}, captures: new Set(),
        classList: {add() {}, remove() {}, contains() {return false;}},
        set innerHTML(v) {this.options = []; this.children = []; for (const match of v.matchAll(/<(span|button|input)\b/g)) this.children.push(element(match[1]));},
        querySelectorAll(tag) {return this.children.flatMap(child => [...(child.tag === tag ? [child] : []), ...child.querySelectorAll(tag)]);},
        addEventListener(name, fn) {(this.handlers[name] ||= []).push(fn);},
        appendChild(v) {this.options.push(v);this.children.push(v);},
        setPointerCapture(id) {this.captures.add(id);}, hasPointerCapture(id) {return this.captures.has(id);},
        releasePointerCapture(id) {this.captures.delete(id);},
        setAttribute(name, value) {this[name] = value;}, focus() {this.focused = true;},
        getContext() {return {translate() {}, rotate() {}, drawImage() {}};},
        toDataURL() {return 'data:rotated';}, clientWidth: 480};
}
const node = id => {
    if (!elements.has(id)) elements.set(id, element());
    return elements.get(id);
};
const context = vm.createContext({
    document: {getElementById: node, createElement: element,
        addEventListener(name, fn) {(documentHandlers[name] ||= []).push(fn);}},
    window: {addEventListener(name, fn) {windowHandlers[name] = fn;}}, TextEncoder, console, clearTimeout,
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
        else if (path.startsWith('/api/list')) {if(listBusy){ok=false;body={error:'设备忙碌',device_status:-9};}else body = JSON.parse(JSON.stringify(selectionListing));}
        else if (path.startsWith('/api/info')) {
            if (infoError) throw infoError;
            body = holdInfo ? await new Promise(resolve => infoReplies.push(resolve)) :
                path.includes('probe=1') && probes.length ? probes.shift() : {...info, uptime_ms: info.uptime_ms + clock - 1000000};
            if (body instanceof Error) throw body;
        }
        else if (path === '/api/remote-pin') body = {ok: true};
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
            if (ok) {
                info = {...info, slideshow_enabled: request.enabled, slideshow_interval_min: request.interval_min, ...('tap_wake' in request ? {tap_wake: request.tap_wake} : {})};
                body = {...body, info: {...info, slideshow_state: request.enabled ? 'usb_paused' : 'off'}};
            }
        }
        else if (path === '/api/slideshow-selection') {
            const request = JSON.parse(options.body);
            body = await new Promise(resolve => selectionRequests.push({body: request, resolve}));
            ok = body.ok !== false;
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
    run("cur='prepared-image';orderDirty=true;editingSlideshow=true;selectionDirty=true");
    node('tab-library').onclick();
    assert.equal(node('view-upload').hidden, true);
    assert.equal(node('view-library').hidden, false);
    assert.equal(node('tab-library')['aria-selected'], 'true');
    assert.equal(run('cur'), 'prepared-image', 'switching tasks preserves the prepared image');
    assert.equal(run('orderDirty&&editingSlideshow&&selectionDirty'), true, 'switching tasks preserves drafts');
    node('tab-library').onkeydown({key: 'ArrowRight', preventDefault() {}});
    assert.equal(node('view-display').hidden, false);
    assert.equal(node('tab-display').tabIndex, 0);
    assert.equal(node('tab-display').focused, true);
    node('returnupload').onclick();
    assert.equal(node('view-upload').hidden, false);
    assert.equal(node('view-display').hidden, true);
    run("cur=null;orderDirty=false;editingSlideshow=false;selectionDirty=false");
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

    node('remote-pin').value = '12a4';
    await node('save-remote-pin').onclick();
    assert.equal(httpRequests.filter(r => r.path === '/api/remote-pin').length, 0);
    node('remote-pin').value = '0123';
    await node('save-remote-pin').onclick();
    assert.equal(httpRequests.filter(r => r.path === '/api/remote-pin').length, 0, 'confirmation is required');
    confirmed = true;
    await node('save-remote-pin').onclick();
    assert.deepEqual(httpRequests.filter(r => r.path === '/api/remote-pin').at(-1).body, {port: 'FAKE', pin: '0123'});
    assert.equal(node('remote-pin').value, '', 'PIN is cleared after saving');
    assert.match(node('remote-pin-status').textContent, /重启/);
    confirmed = false;

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
    assert.equal(node('sessionnotice').hidden, false, 'locked settings explain the active upload session');
    run('batchActive=false');
    node('tapwake').checked = false; node('tapwake').onchange();
    const failWake = node('saveslide').onclick(); await tick();
    slideshowRequests[3].resolve({ok: false, error: 'write failed'}); await failWake;
    assert.equal(node('tapwake').checked, false, 'failed save keeps the draft');
    assert.equal(run('deviceInfo.tap_wake'), true, 'failed save keeps the device state');

    // Start the independent selection checks with no outstanding timer draft.
    run('editingSlideshow=false');
    // Per-picture selection is independent of manual display and gallery order.
    assert.equal(node('selectall').disabled, true, 'older firmware cannot change selection');
    info = {...info, slideshow_selection_supported: true};
    selectionListing = {connected:true, selection:{token:'0000000000000001',custom:false,fault:false},
        items:['a.bin','b.bin','c.bin','d.bin'].map(id=>({id,label:id,selected:true}))};
    await run('refresh()');
    assert.equal(node('selectall').disabled, false);
    assert.equal(node('saveselection').disabled, true);
    const checks = () => node('cards').querySelectorAll('input');
    assert.equal(checks().length,4);
    assert(checks().every(c=>c.checked));
    checks()[1].checked=false; checks()[1].onchange();
    checks()[3].checked=false; checks()[3].onchange();
    assert.equal(run('selectionNames.size'),2);
    await run('refresh()');
    assert.deepEqual(checks().map(c=>c.checked),[true,false,true,false], 'polls preserve draft by filename');
    const buttons=node('cards').children[1].querySelectorAll('button');
    buttons[2].onclick(); await tick();
    assert(httpRequests.some(r=>r.path==='/api/show'&&r.body.name==='b.bin'), 'unselected B remains displayable');
    const oldSelection=run('selectionMeta.token');
    const saveSelection=node('saveselection').onclick(); await tick();
    assert.deepEqual(selectionRequests[0].body,{port:'SECOND',token:oldSelection,names:['a.bin','c.bin']});
    for(const id of ['saveselection','selectall','selectnone','saveslide','send','port','refresh'])
        assert.equal(node(id).disabled,true, `${id} frozen during selection save`);
    assert(checks().every(c=>c.disabled));
    selectionRequests[0].resolve({ok:true,selection:{token:'0000000000000002',custom:true,fault:false,
        items:selectionListing.items.map(it=>({...it,selected:['a.bin','c.bin'].includes(it.id)}))}});
    await saveSelection;
    assert.equal(run('selectionDirty'),false);
    assert.deepEqual(checks().map(c=>c.checked),[true,false,true,false]);
    node('selectnone').onclick();
    assert.equal(run('selectionNames.size'),0);
    const failSelection=node('saveselection').onclick();await tick();
    selectionRequests[1].resolve({ok:false,error:'disk full'});await failSelection;
    assert.equal(run('selectionDirty'),true);
    assert.equal(run('selectionNames.size'),0,'failed save preserves explicit empty draft');
    assert.equal(run('selectionMeta.token'),'0000000000000002');
    run('batchActive=true;syncControls()');
    assert(checks().every(c=>c.disabled));assert.equal(node('saveselection').disabled,true);
    run('batchActive=false;syncControls()');
    node('selectall').onclick();assert.equal(run('selectionNames.size'),4);
    // Drafts stay keyed to names when order changes and new uploads arrive.
    checks()[1].checked=false;checks()[1].onchange();
    selectionListing.items.reverse();selectionListing.items.push({id:'new.bin',label:'new',selected:false});
    await run('refresh()');
    assert.equal(run('selectionNames.has("b.bin")'),false);
    assert.equal(run('selectionNames.has("new.bin")'),false);
    assert.equal(run('selectionNames.size'),3);
    // Reproduce the misleading checked+20min UI while the device is still off.
    info = {...info, slideshow_enabled:false, slideshow_interval_min:60, slideshow_state:'off'};
    await run('refresh()');
    node('slideshow').checked=true;node('slideshow').onchange();
    node('slideinterval').value='20';node('slideinterval').oninput();
    assert(node('slidestatus').textContent.includes('设备当前已关闭'));
    assert(node('slidestatus').textContent.includes('60 分钟'));
    const timerWrites=slideshowRequests.length, selectionWrites=selectionRequests.length;
    assert.equal(node('saveselection').textContent,'保存定时设置及轮播名单');
    let prevented=false;
    windowHandlers.beforeunload({preventDefault(){prevented=true;}});
    assert(prevented,'leaving warns about unsaved timer or selection');

    const failedCombined=node('saveselection').onclick();await tick();
    assert.equal(slideshowRequests.length,timerWrites+1);
    assert.equal(selectionRequests.length,selectionWrites,'timer is confirmed before submitting selection');
    slideshowRequests.at(-1).resolve({ok:false,error:'设备读回不一致'});await failedCombined;
    assert.equal(run('editingSlideshow'),true);
    assert.equal(run('selectionDirty'),true);
    assert.equal(selectionRequests.length,selectionWrites,'failed timer save leaves the list unsubmitted');
    assert(node('msg').textContent.includes('定时设置保存未确认，名单未提交'));

    const combined=node('saveselection').onclick();await tick();
    assert.deepEqual(slideshowRequests.at(-1).body,{port:'SECOND',enabled:true,interval_min:20,tap_wake:true});
    slideshowRequests.at(-1).resolve({ok:true});await tick();
    assert.equal(run('editingSlideshow'),false);
    assert.equal(run('deviceInfo.slideshow_enabled'),true);
    assert.equal(run('deviceInfo.slideshow_interval_min'),20);
    assert.equal(selectionRequests.length,selectionWrites+1);
    assert.equal(node('saveslide').disabled,true,'controls stay frozen across both writes');
    selectionRequests.at(-1).resolve({ok:false,error:'名单写入失败'});await combined;
    assert.equal(run('selectionDirty'),true);
    assert(node('msg').textContent.includes('定时设置已保存并读回确认，但轮播名单保存未确认'));
    const finalSelection=node('saveselection').onclick();await tick();
    assert.equal(slideshowRequests.length,timerWrites+2,'retry only writes the pending list');
    selectionRequests.at(-1).resolve({ok:true,selection:{...selectionListing.selection,items:selectionListing.items}});
    await finalSelection;
    assert.equal(run('selectionDirty'),false);
    assert.equal(node('msg').dataset.kind,'success');
    assert.equal(node('msg').hidden,false);
    prevented=false;windowHandlers.beforeunload({preventDefault(){prevented=true;}});
    assert.equal(prevented,false,'confirmed settings do not warn on leaving');

    // Input events with an unchanged value must not re-dirty a confirmed save.
    node('slideinterval').oninput();node('slideshow').onchange();node('tapwake').onchange();
    assert.equal(run('editingSlideshow'),false);
    assert(!node('slidestatus').textContent.includes('尚未保存'));

    // Even without list changes, the combined button can save a timer draft.
    node('slideinterval').value='21';node('slideinterval').oninput();
    assert.equal(node('saveselection').disabled,false);
    const timerOnly=node('saveselection').onclick();await tick();
    slideshowRequests.at(-1).resolve({ok:true});await tick();
    selectionRequests.at(-1).resolve({ok:true,selection:{...selectionListing.selection,items:selectionListing.items}});
    await timerOnly;
    assert.equal(run('deviceInfo.slideshow_interval_min'),21);
    assert.equal(run('editingSlideshow'),false);
    assert(node('msg').textContent.includes('每 21 分钟'));

    // Saving just a list with playback off must say that playback remains off.
    info={...info,slideshow_enabled:false};await run('refresh()');
    node('selectnone').onclick();
    const disabledSelection=node('saveselection').onclick();await tick();
    selectionRequests.at(-1).resolve({ok:true,selection:{...selectionListing.selection,items:selectionListing.items.map(it=>({...it,selected:false}))}});
    await disabledSelection;
    assert(node('msg').textContent.includes('定时换图当前关闭'));
    assert.equal(info.slideshow_enabled,false);

    node('port').value='THIRD';await run('refresh()');
    assert.equal(run('selectionDirty'),false);
    assert.equal(run('selectionNames.has("b.bin")'),true,'another device uses its saved list');
    assert.equal(run('selectionNames.has("new.bin")'),false);

    // Smart guides use CSS-pixel thresholds and preserve snapped placement.
    const event = (type, x=0, y=0, extra={}) => {
        const ev = {type, clientX:x, clientY:y, pointerId:1, button:0, isPrimary:true, altKey:false, ...extra};
        for (const fn of node('edit').handlers[type] || []) fn(ev);
    };
    const key = (type) => {
        for (const fn of documentHandlers[type] || []) fn({key:'Alt'});
    };
    const begin = (width=300, height=500) => {
        run(`src=null;rawImg={naturalWidth:${width},naturalHeight:${height}};rw=${width};rh=${height};rot=0;sc=1;ox=-100;oy=-100;cur='before-drag'`);
        node('frame').clientWidth=240;
        node('snap').checked=true;
        event('pointerdown');
        assert.equal(run('cur'),null,'dragging invalidates the previously prepared frame');
    };
    for (const [x,y,gx,gy] of [[0,0,'left','top'],[90,110,'center-x','center-y'],[180,220,'right','bottom']]) {
        begin();
        const mx=(x+100+8)/2,my=(y+100-8)/2;
        event('pointermove',mx,my);
        assert.equal(run('ox'),x);assert.equal(run('oy'),y);
        assert.equal(node('snap-'+gx).hidden,false);
        assert.equal(node('snap-'+gy).hidden,false);
        event('pointerup',mx,my);run('clearTimeout(ctimer)');
        assert.equal(run('ox'),x);assert.equal(run('oy'),y,'release retains the snapped coordinates');
        assert.equal(node('snap-'+gx).hidden,false,'guides remain visible after release');
        assert.equal(node('edit').captures.size,0);
    }
    // Hysteresis holds an acquired guide at 9 CSS px, then releases at 12.
    begin();event('pointermove',99,101);
    assert.equal(run('ox'),90);
    event('pointermove',104,101);assert.equal(run('ox'),90);
    event('pointermove',107,101);assert.equal(run('ox'),114);
    assert.equal(node('snap-center-x').hidden,true);
    event('pointercancel');run('clearTimeout(ctimer)');

    begin();event('pointermove',99,101);
    key('keydown');assert.equal(run('ox'),98,'Alt releases snapping without moving the pointer');
    assert.equal(node('snap-center-x').hidden,true);
    key('keyup');assert.equal(run('ox'),90,'releasing Alt reacquires the guide');
    node('snap').checked=false;node('snap').onchange();
    assert.equal(run('ox'),98,'turning off snapping preserves the unsnapped pointer position');
    event('pointerup',99,101);run('clearTimeout(ctimer)');
    assert.equal(run('ox'),98);
    assert.equal(node('snap-center-x').hidden,true);

    // Oversized images also align their center and far edges with the frame.
    begin(800,1000);event('pointermove',-26,-24);
    assert.equal(run('ox'),-160);assert.equal(run('oy'),-140);
    event('pointerup',-26,-24);run('clearTimeout(ctimer)');
    begin(800,1000);event('pointermove',-106,-94);
    assert.equal(run('ox'),-320);assert.equal(run('oy'),-280);
    event('pointerup',-106,-94);run('clearTimeout(ctimer)');

    // Threshold stays 6 CSS px when the editor is shown at panel resolution.
    begin();node('frame').clientWidth=480;event('pointermove',198,202);
    assert.equal(run('ox'),98,'8 CSS px is outside the acquisition radius');
    event('pointercancel');run('clearTimeout(ctimer)');
    begin();event('pointermove',99,101,{pointerId:2});
    assert.equal(run('ox'),-100,'another touch must not move the active drag');
    event('lostpointercapture');run('clearTimeout(ctimer)');
    event('pointermove',99,101);assert.equal(run('ox'),-100,'capture loss ends the drag');

    // Rotation uses the rotated bounding box; saved/API coordinates match guides.
    begin();run('rot=90;rawImg={naturalWidth:500,naturalHeight:300};rotImage()');
    event('pointermove',99,101);event('pointerup',99,101);run('clearTimeout(ctimer)');
    run('savePlace()');
    const savedSnap=httpRequests.at(-1).body.placements;
    assert.equal(Object.values(savedSnap)[0].ox,90);
    assert.equal(Object.values(savedSnap)[0].oy,110);
    run("src='data:snapped';convert()");await tick();
    assert.deepEqual(requests.at(-1).body.place,{rotate:270,scale:1,ox:90,oy:110});
    requests.at(-1).resolve(result('snapped'));await tick();
    assert.equal(run('cur'),'snapped');
    assert.equal(node('snap-center-x').hidden,false,'conversion does not remove alignment feedback');
    node('bfill').onclick();run('clearTimeout(ctimer)');
    assert.equal(node('snap-center-x').hidden,true,'a different transform clears obsolete guides');
})().catch(error => {console.error(error); process.exitCode = 1;});
