// Run the actual inline page script with controlled asynchronous HTTP replies.
const assert = require('node:assert/strict');
const vm = require('node:vm');
const script = require('node:fs').readFileSync(0, 'utf8');
const elements = new Map(), requests = [], readers = [], images = [], uploads = [];
let finishUpload;
function element() {
    return {value: '', style: {}, dataset: {}, disabled: true, options: [],
        classList: {add() {}, remove() {}, contains() {return false;}},
        addEventListener() {}, appendChild() {}, setPointerCapture() {},
        getContext() {return {translate() {}, rotate() {}, drawImage() {}};},
        toDataURL() {return 'data:rotated';}, clientWidth: 480};
}
const node = id => {
    if (!elements.has(id)) elements.set(id, element());
    return elements.get(id);
};
const context = vm.createContext({
    document: {getElementById: node, createElement: element, addEventListener() {}},
    window: {addEventListener() {}}, TextEncoder, console, setTimeout, clearTimeout,
    setInterval() {}, devicePixelRatio: 1,
    FileReader: class {readAsDataURL(file) {this.file = file; readers.push(this);}},
    Image: class {
        constructor() {this.naturalWidth = 480; this.naturalHeight = 720;}
        set src(v) {this.value = v; images.push(this);}
    },
    fetch: async (path, options) => {
        let body;
        if (path === '/api/convert') {
            body = await new Promise((resolve, reject) => requests.push({
                body: JSON.parse(options.body), resolve, reject}));
        } else if (path === '/api/ports') body = {ports: [], default: null};
        else if (path === '/api/settings') body = {settings: {}, panel_mm: [52.1,78.2]};
        else if (path === '/api/send') {uploads.push(JSON.parse(options.body)); body = {job: 'upload'};}
        else if (path === '/api/job?id=upload') body = await new Promise(resolve => {finishUpload = resolve;});
        else if (path.startsWith('/api/list')) body = {connected: true, items: []};
        else if (path.startsWith('/api/info')) body = {fw: '1.3.4', battery: 80, free_kb: 1024};
        else throw new Error('Unexpected request: ' + path);
        return {ok: true, json: async () => body};
    }
});
const run = text => vm.runInContext(text, context);
const tick = () => new Promise(resolve => setImmediate(resolve));
const result = id => ({id, preview: id, name: id, coverage: 1});

(async () => {
    run(script);
    await tick();
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
    const duringUpload = run('convert()'); await tick();
    requests[5].resolve(result('new-preview-during-upload')); await duringUpload;
    assert.equal(node('send').disabled, true, 'converting cannot enable a second concurrent upload');
    finishUpload({state: 'ok', done: 1, total: 1}); await upload;
    assert.equal(run('cur'), 'new-preview-during-upload');
    assert.equal(node('send').disabled, false, 'latest valid preview remains sendable after upload');
})().catch(error => {console.error(error); process.exitCode = 1;});
