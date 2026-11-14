// Run web/app.js with controlled DOM events and asynchronous HTTP replies.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const script = require('node:fs').readFileSync(0, 'utf8');
const watchdog = setTimeout(() => {console.error('Unfinished browser regression'); process.exit(1);}, 15000);
const elements = new Map(), requests = [], readers = [], images = [], uploads = [];
const httpRequests = [], probes = [], stripRequests = [], infoReplies = [];
const planRequests = [], slideshowRequests = [], selectionRequests = [], windowHandlers = {}, documentHandlers = {};
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
        else if (path === '/api/show') { info = {...info, name: JSON.parse(options.body).name}; body = {ok:true}; }
        else if (path === '/api/slideshow-plan') {
            body = await new Promise(resolve => planRequests.push({body:JSON.parse(options.body),resolve}));
            if (body.info) info = {...body.info};
            if (body.selection) selectionListing = {connected:true,selection:body.selection,items:body.selection.items};
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

    // One save covers all visible drafts; the summary always uses confirmed data.
    info = {...info, slideshow_supported:true, slideshow_enabled:true, slideshow_interval_min:20,
        slideshow_state:'usb_paused', slideshow_behavior_version:2, tap_wake_supported:true,
        tap_wake:false, remote_sleep_managed:false, slideshow_selection_supported:true, name:'a.bin', n:4};
    selectionListing = {connected:true,selection:{token:'0000000000000001',custom:false,fault:false},
        items:['a.bin','b.bin','c.bin','d.bin'].map(id=>({id,label:id,selected:true}))};
    run('orderDirty=editingSlideshow=selectionDirty=planConflict=planStateUnknown=false;batchActive=false');
    await run('refresh()');
    assert(node('playback-status').textContent.includes('暂停'));
    assert(node('playback-next').textContent.includes('b.bin'));
    assert(node('playback-note').textContent.includes('新图自动加入'));
    const checks=()=>node('cards').querySelectorAll('input');
    checks()[1].checked=false;checks()[1].onchange();
    node('slideinterval').value='30';node('slideinterval').oninput();
    node('cards').children[3].querySelectorAll('button')[0].onclick();
    assert.equal(run('planDirty()'),true);
    assert(node('playback-plan').textContent.includes('20 分钟'),'confirmed interval survives draft');
    assert(node('playback-next').textContent.includes('b.bin'),'draft selection/order do not alter planned next');
    let prevented=false;windowHandlers.beforeunload({preventDefault(){prevented=true;}});
    assert(prevented,'all draft types protect navigation');
    confirmed=false;await node('refresh').onclick();
    assert.equal(run('planDirty()'),true,'cancelled reload preserves drafts');
    node('port').value='SECOND';await node('port').onchange();
    assert.equal(node('port').value,'FAKE','cancelled device switch restores port');
    const combined=node('saveplan').onclick();await tick();
    assert.deepEqual(planRequests[0].body,{port:'FAKE',token:'0000000000000001',
        order:['a.bin','b.bin','d.bin','c.bin'],names:['a.bin','d.bin','c.bin'],
        settings:{enabled:true,interval_min:30,tap_wake:false}});
    assert.equal(node('saveplan').disabled,true);
    planRequests[0].resolve({ok:false,confirmed:['order','selection'],failed:'settings',error:'写卡失败',
        info:{...info},selection:{token:'0000000000000002',custom:true,fault:false,
            items:['a.bin','b.bin','d.bin','c.bin'].map(id=>({id,selected:id!=='b.bin'}))}});
    await combined;
    assert.equal(run('orderDirty||selectionDirty'),false);
    assert.equal(run('editingSlideshow'),true);
    assert(node('msg').textContent.includes('已保存：图库顺序、轮播名单'));
    assert(node('playback-next').textContent.includes('d.bin'));
    assert(node('playback-plan').textContent.includes('20 分钟'));
    const retry=node('saveplan').onclick();await tick();
    assert.equal('names' in planRequests[1].body,false,'retry only submits unconfirmed changes');
    planRequests[1].resolve({ok:true,confirmed:['settings'],info:{...info,slideshow_interval_min:30},
        selection:selectionListing.selection});await retry;
    assert.equal(run('planDirty()'),false);
    assert.equal(node('msg').dataset.kind,'success');
    assert(node('playback-plan').textContent.includes('30 分钟'));
    assert(node('playback-note').textContent.includes('新图需手动勾选'));
    // Timer-only saves never turn the default dynamic list into a static list.
    selectionListing.selection.custom=false;await run('refresh()');
    node('slideinterval').value='31';node('slideinterval').oninput();
    const timerOnly=node('saveplan').onclick();await tick();
    assert.equal('names' in planRequests[2].body,false);
    planRequests[2].resolve({ok:true,confirmed:['settings'],info:{...info,slideshow_interval_min:31},selection:selectionListing.selection});await timerOnly;
    // Summary handles explicit empty and sole-current lists without a misleading "running" state.
    selectionListing.items=selectionListing.items.map(it=>({...it,selected:false}));
    selectionListing.selection={...selectionListing.selection,token:'0000000000000003',custom:true};
    await run('refresh()');assert(node('playback-status').textContent.includes('没有参与图片'));
    selectionListing.items[0].selected=true;await run('refresh()');
    assert(node('playback-status').textContent.includes('保持当前图片'));
    info={...info,remote_sleep_managed:true};await run('refresh()');
    assert.equal(node('tapwake').disabled,true);
    assert(node('tapwakestatus').textContent.includes('自动管理'));
    info={...info,slideshow_behavior_version:undefined};await run('refresh()');
    assert(node('playback-note').textContent.includes('旧固件'));
    // A lost save response keeps the draft and requires readback before retry.
    node('selectnone').onclick();const uncertain=node('saveplan').onclick();await tick();
    planRequests.at(-1).resolve({ok:false,confirmed:[],failed:'selection',error:'无应答',state_unknown:true});await uncertain;
    assert.equal(run('selectionDirty'),true);assert.equal(node('saveplan').disabled,true);
    await run('refresh()');assert.equal(run('planStateUnknown'),false);
    // Incoming gallery changes protect all drafts until explicitly reloaded.
    selectionListing.selection.token='0000000000000004';await run('refresh()');
    assert.equal(run('planConflict'),true);assert.equal(node('saveplan').disabled,true);
    confirmed=true;await node('refresh').onclick();confirmed=false;
    assert.equal(run('planDirty()||planConflict'),false);
    // Older slideshow firmware has no selection protocol: its implicit list is all images.
    info={...info,slideshow_selection_supported:false,slideshow_behavior_version:undefined};
    selectionListing={connected:true,items:selectionListing.items.map(({id,label})=>({id,label}))};
    await run('refresh()');
    assert(!node('playback-status').textContent.includes('没有参与图片'));
    assert.equal(run('savedItems.filter(it=>it.selected).length'),4);
    run('connected=false;syncControls()');
    assert(node('playback-status').textContent.includes('最后确认'));
    run('connected=true');

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
})().then(() => {clearTimeout(watchdog);console.log('OK browser conversion, unified plan, drafts, polling and save feedback');}).catch(error => {clearTimeout(watchdog);console.error(error);process.exitCode=1;});
