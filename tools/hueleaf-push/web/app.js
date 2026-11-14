// Shared state and task navigation. No frontend build step is required.
const $ = (id) => document.getElementById(id);
let cur = null,
    src = null,
    fname = "",
    items = [],
    connected = false,
    sending = false,
    finishing = false,
    batchActive = false;
let deviceInfo = null,
    infoSeenAt = 0,
    orderDirty = false,
    refreshSeq = 0,
    changingDisplay = false,
    changingPin = false,
    keepingAlive = false,
    changingSlideshow = false,
    editingSlideshow = false,
    slideshowPort = "";
const msg = (text, kind = "error") => {
    const el = $("msg");
    el.textContent = text || "";
    el.dataset.kind = kind;
    el.hidden = !text;
};
const views = ["upload", "library", "display"];
function showView(view, focus = false) {
    if (!views.includes(view)) return;
    for (const name of views) {
        const active = name === view,
            tab = $("tab-" + name);
        $("view-" + name).hidden = !active;
        tab.setAttribute("aria-selected", String(active));
        tab.tabIndex = active ? 0 : -1;
    }
    if (focus) $("tab-" + view).focus();
    if (view === "upload") layout();
}
for (const [index, view] of views.entries()) {
    const tab = $("tab-" + view);
    tab.onclick = () => showView(view);
    tab.onkeydown = (e) => {
        let next;
        if (e.key === "ArrowRight") next = (index + 1) % views.length;
        else if (e.key === "ArrowLeft") next = (index + views.length - 1) % views.length;
        else if (e.key === "Home") next = 0;
        else if (e.key === "End") next = views.length - 1;
        else return;
        e.preventDefault();
        showView(views[next], true);
    };
}
$("returnupload").onclick = () => showView("upload", true);
const NOPORT = "未检测到 HueLeaf，请插上 USB";
let savedItems = [], planConflict = false, planStateUnknown = false;
let selectionMeta = null,
    selectionNames = new Set(),
    selectionDirty = false,
    changingSelection = false;
const busy = () =>
    sending ||
    finishing ||
    changingDisplay ||
    changingPin ||
    keepingAlive ||
    changingSlideshow ||
    changingSelection;
const deviceBusy = () =>
    deviceInfo &&
    ["starting", "refreshing", "maintenance", "fault", "sleeping"].includes(deviceInfo.state);
function planDirty() { return editingSlideshow || selectionDirty || orderDirty; }
function nextSavedPicture() {
    const selected = savedItems.filter(it => it.selected);
    if (!selected.length || !deviceInfo) return null;
    const current = savedItems.findIndex(it => it.id === deviceInfo.name);
    for (let step = 1; step <= savedItems.length; step++) {
        const it = savedItems[(current + step) % savedItems.length];
        if (it.selected) return it;
    }
    return null;
}
function renderPlaybackSummary() {
    const i = deviceInfo, next = nextSavedPicture();
    const current = savedItems.find(it => it.id === (i && i.name));
    const selected = savedItems.filter(it => it.selected).length;
    let status = "未连接设备";
    if (i) {
        const reason = i.slideshow_status || (selectionMeta && selectionMeta.fault ? "fault" :
            !i.slideshow_enabled ? "off" : !savedItems.length ? "empty" : !selected ? "no_selection" :
            selected === 1 && current && current.selected ? "single_hold" : i.slideshow_state);
        status = ({off:"轮播已关闭", fault:"轮播异常，已暂停", starting:"设备正在准备图库",
            empty:"没有图片，不会自动换图", no_selection:"没有参与图片，不会自动换图",
            single_hold:"仅一张参与，保持当前图片", usb_paused:"轮播已开启 · 连接电脑期间暂停",
            busy:"轮播已开启 · 等待当前操作结束", waiting:"轮播已开启 · 等待下次换图"})[reason] ||
            (i.slideshow_enabled ? "轮播已开启" : "轮播已关闭");
    }
    $("playback-status").textContent = !connected && i ? "已断开 · 以下为最后确认的计划" : status;
    $("playback-plan").textContent = i ? `设备设置：${i.slideshow_enabled ? "开启" : "关闭"} · 每 ${i.slideshow_interval_min} 分钟 · ${selected}／${savedItems.length} 张参与` : "连接相框后读取轮播状态。";
    $("playback-next").textContent = i ? `当前显示：${current ? current.label : i.name || "暂无"}${current && !current.selected ? "（不参与轮播）" : ""} · 计划下一张：${next ? next.id === i.name ? "保持当前图片" : next.label : "无"}` : "";
    let note = i && i.slideshow_enabled && next && next.id !== i.name ?
        `拔掉电脑后，若无其他换图操作，约 ${i.slideshow_interval_min} 分钟开始切换到「${next.label}」。` : "";
    if (selectionMeta) note += selectionMeta.custom ? " 自定义名单：新图需手动勾选。" : " 默认全部参与：新图自动加入。";
    if (i) note += i.slideshow_behavior_version >= 2 ? " 查看菜单、拍醒和读取状态不重新计时；实际换图后重新计时。" : " 旧固件：查看菜单、拍醒等操作可能重新计时，状态条开启时可能刷新同图。";
    if (planStateUnknown) note = "设备当前设置尚未确认，请重新读取；这里保留上次确认结果。";
    $("playback-note").textContent = note;
}
// Capability checks and operation locks remain shared across all three views.
function syncControls() {
    const ready = connected && deviceInfo && deviceInfo.ready !== false;
    $("sessionnotice").hidden = !batchActive;
    $("uploadstate").textContent = sending
        ? "正在发送"
        : finishing
          ? "正在结束传图"
          : batchActive
            ? "传图会话中"
            : cur
              ? "可发送"
              : "待选择图片";
    $("orderstatus").textContent = orderDirty
        ? "顺序已调整，尚未保存；请点「保存顺序」"
        : "列表顺序同时用于手动选图和定时轮播";
    $("orderstatus").dataset.dirty = String(orderDirty);
    const i = deviceInfo;
    if (connected && i) {
        $("dev").textContent =
            `固件 ${i.fw} · 电量 ${i.battery < 0 ? "—" : i.battery + "%"} · 剩余 ${i.storage_cache_valid === false ? "读取中…" : Math.round(i.free_kb / 1024) + " MB"}${i.state ? " · " + ({ starting: "启动中", idle: "空闲", refreshing: "刷屏中", uploading: "传图中", maintenance: "维护中", fault: "存储故障" }[i.state] || i.state) : ""}`;
    } else $("dev").textContent = "";
    $("save-remote-pin").disabled = busy() || batchActive || !connected;
    $("remote-pin").disabled = busy() || batchActive || !connected;
    const stripKnown = ready && typeof deviceInfo.strip === "boolean",
        fullimage = $("fullimage");
    fullimage.disabled = busy() || batchActive || deviceBusy() || !stripKnown;
    if (!changingDisplay) {
        fullimage.checked = !!stripKnown && !deviceInfo.strip;
        fullimage.indeterminate = !stripKnown;
    }
    $("displaystatus").textContent = changingDisplay
        ? "正在设置，请等待相框刷新…"
        : !connected
          ? "连接相框后可设置"
          : !ready
            ? "设备尚未就绪"
            : !stripKnown
              ? "设备未提供状态栏设置"
              : deviceInfo.strip
                ? "当前显示底部状态栏"
                : "纯图片显示已启用";
    const slideKnown =
        ready &&
        deviceInfo.slideshow_supported === true &&
        typeof deviceInfo.slideshow_enabled === "boolean" &&
        Number.isInteger(deviceInfo.slideshow_interval_min);
    for (const id of ["slideshow", "slideinterval"])
        $(id).disabled = busy() || batchActive || deviceBusy() || !slideKnown;
    const tapWakeKnown =
        slideKnown &&
        deviceInfo.tap_wake_supported === true &&
        typeof deviceInfo.tap_wake === "boolean";
    $("tapwake").disabled = busy() || batchActive || deviceBusy() || !tapWakeKnown || !!deviceInfo.remote_sleep_managed;
    if (slideKnown && !changingSlideshow && !editingSlideshow) {
        $("slideshow").checked = deviceInfo.slideshow_enabled;
        $("slideinterval").value = deviceInfo.slideshow_interval_min;
        $("tapwake").checked = tapWakeKnown && deviceInfo.tap_wake;
    }
    $("tapwakestatus").textContent = deviceInfo && deviceInfo.remote_sleep_managed
        ? "蓝牙遥控模式自动管理拍醒；关闭轮播不会关闭遥控或拍醒。"
        : !connected
        ? "连接相框后可设置拍击唤醒"
        : !ready
          ? "设备尚未就绪"
          : !tapWakeKnown
            ? "当前固件不支持拍击唤醒，请升级固件"
            : deviceInfo.slideshow_behavior_version >= 2
              ? "普通轮播模式：正常拍醒保留画面，换图时间不变；开启会增加待机耗电。"
              : "旧固件：拍醒可能刷新当前图并重新计时；开启会增加待机耗电。";
    $("slidestatus").textContent = changingSlideshow
        ? "正在保存定时设置…"
        : !connected
          ? "连接相框后可设置"
          : !ready
            ? "设备尚未就绪"
            : !slideKnown
              ? "当前固件不支持定时换图，请升级固件"
              : editingSlideshow
                ? `定时设置尚未保存（设备当前${deviceInfo.slideshow_enabled ? "已开启" : "已关闭"}，间隔 ${deviceInfo.slideshow_interval_min} 分钟）；请点「保存更改」`
                : !deviceInfo.slideshow_enabled
                  ? "定时换图已关闭"
                  : {
                        usb_paused: "已启用，连接电脑期间暂停",
                        fault: "定时换图发生错误，已保持唤醒；请检查设备日志",
                        empty: "图库为空，已保持唤醒",
                        awake_window: "已启用，操作窗口结束后进入深睡"
                    }[deviceInfo.slideshow_state] || "定时深睡轮播已启用";
    $("send").disabled = !cur || busy() || deviceBusy() || !ready || !deviceInfo.upload_no_refresh;
    $("finish").disabled = busy() || !batchActive || !ready;
    $("port").disabled = busy() || batchActive;
    $("rescan").disabled = busy();
    $("refresh").disabled = busy();

    $("uploadhint").textContent =
        connected && deviceInfo && !deviceInfo.upload_no_refresh
            ? "设备固件不支持静默上传，请先升级到 1.4.0 或更新版本"
            : batchActive
              ? "图片已逐张保存，可继续发送。全部完成后点「结束传图」，恢复按键、图库操作和相框设置，画面保持原样。"
              : "可逐张连续发送；全部完成后点「结束传图」，恢复相框按键，画面保持原样。";
    if (
        connected &&
        deviceInfo &&
        deviceInfo.upload_no_refresh &&
        !deviceInfo.upload_session_supported
    )
        $("uploadhint").textContent += " 当前固件缺少整批按键保护，升级到 1.5.0 后可用。";
    const selectionKnown =
        ready && deviceInfo.slideshow_selection_supported === true && selectionMeta;
    for (const id of ["selectall", "selectnone"])
        $(id).disabled =
            busy() ||
            batchActive ||
            deviceBusy() ||
            !selectionKnown;
    $("selectionstatus").textContent = changingSelection
        ? "正在保存轮播名单…"
        : !connected
          ? "连接后可勾选轮播图片"
          : !ready
            ? "设备尚未就绪"
            : !deviceInfo.slideshow_selection_supported
              ? "当前固件不支持指定轮播图片，请升级固件"
              : !selectionMeta
                ? "正在读取轮播名单…"
                : `${selectionNames.size}／${items.length} 张参与定时轮播${selectionDirty ? "（尚未保存）" : selectionMeta.fault ? "（名单读取异常，自动轮播暂停；请重新勾选保存）" : selectionMeta.custom ? "（自定义名单）" : "（默认全部参与）"}`;
    $("saveplan").disabled = busy() || batchActive || deviceBusy() || !ready ||
        !planDirty() || planConflict || planStateUnknown;
    $("plan-dirty").textContent = planStateUnknown ? "保存结果尚未确认，请重新读取设备" :
        planConflict ? "图库已变化，请重新读取并核对修改" : planDirty() ? "有未保存的修改" : !connected ? "连接相框后可编辑" : "所有更改已保存";
    renderPlaybackSummary();
    for (const b of $("cards").querySelectorAll("button"))
        b.disabled = busy() || batchActive || deviceBusy() || !ready;
    for (const b of $("cards").querySelectorAll("input"))
        b.disabled = busy() || batchActive || deviceBusy() || !selectionKnown;
}
// Same-origin JSON API and USB port discovery.
async function api(path, body) {
    let r;
    try {
        r = await fetch(
            path,
            body
                ? {
                      method: "POST",
                      headers: { "Content-Type": "application/json" },
                      body: JSON.stringify(body)
                  }
                : {}
        );
    } catch (e) {
        throw new Error("界面和后台程序断开了，请重新运行启动脚本");
    }
    const j = await r.json().catch(() => ({ error: "操作失败，请重试" }));
    if (!r.ok) {
        const e = new Error(j.error || "操作失败，请重试");
        e.deviceStatus = j.device_status;
        throw e;
    }
    return j;
}
$("save-remote-pin").onclick = async () => {
    const pin = $("remote-pin").value;
    if (!/^[0-9]{4}$/.test(pin)) { $("remote-pin-status").textContent = "请输入恰好 4 位数字。"; return; }
    if (!confirm("设置 PIN 会替换设备原有随机密钥，使旧手机凭据失效。4 位 PIN 可被离线穷举；确认继续？")) return;
    changingPin = true;
    syncControls();
    $("remote-pin-status").textContent = "正在通过 USB 写入…";
    try {
        await api("/api/remote-pin", {port: $("port").value, pin});
        $("remote-pin").value = "";
        $("remote-pin-status").textContent = "已保存。请重启相框后，在手机输入相同 PIN。";
    } catch (e) {
        $("remote-pin-status").textContent = e.message;
    } finally {
        changingPin = false;
        syncControls();
    }
};
let lastPorts = "";
async function ports(manual) {
    let j;
    try {
        j = await api("/api/ports");
    } catch (e) {
        return;
    }
    if (busy() || batchActive) return j;
    const key = JSON.stringify(j.ports);
    const s = $("port");
    const had = s.value;
    if (key !== lastPorts || manual) {
        lastPorts = key;
        s.innerHTML = "";
        const ph = document.createElement("option");
        ph.value = "";
        ph.textContent = j.ports.length ? "— 选择端口 —" : "（无可用端口）";
        s.appendChild(ph);
        for (const p of j.ports) {
            const o = document.createElement("option");
            o.value = p.device;
            o.textContent =
                (p.hueleaf ? "★ HueLeaf " : "") +
                p.device +
                (p.desc && p.desc !== "n/a" ? " " + p.desc : "");
            s.appendChild(o);
        }
        if (had && planDirty() && !j.ports.some(p => p.device === had)) {
            const lost = document.createElement("option"); lost.value = had;
            lost.textContent = had + "（已断开，修改已保留）"; s.appendChild(lost);
            connected = false;
        }
        s.value = [...s.options].some((o) => o.value === had) && had ? had : "";
    }
    if (!s.value && j.default) {
        s.value = j.default;
    }
    $("notice").textContent = j.ports.some((p) => p.hueleaf) ? "" : NOPORT;
    if (s.value !== had || manual) await refresh();
    return j;
}
function setPane(img, url) {
    img.src = url;
    if (img.nextElementSibling && img.nextElementSibling.tagName === "SPAN")
        img.nextElementSibling.style.display = "none";
}
/* ---- free placement editor: panel px coordinates, same rounding as hlformat.placement_box ---- */
const PW = 480,
    PH = 720;
let PMM = [52.1, 78.2],
    pxmm = 96 / 25.4,
    settings = {};
let rawImg = null,
    rot = 0,
    sc = 1,
    ox = 0,
    oy = 0,
    ikey = "",
    rotUrl = "",
    rw = 0,
    rh = 0;
function rotDims() {
    return rot % 180
        ? [rawImg.naturalHeight, rawImg.naturalWidth]
        : [rawImg.naturalWidth, rawImg.naturalHeight];
}
function preset(m) {
    clearSnapGuides();
    [rw, rh] = rotDims();
    sc = (m === "pad" ? Math.min : Math.max)(PW / rw, PH / rh);
    ox = (PW - rw * sc) / 2;
    oy = (PH - rh * sc) / 2;
}
function fillScale() {
    return Math.max(PW / rw, PH / rh);
}
function rotImage() {
    const c = document.createElement("canvas");
    [rw, rh] = rotDims();
    c.width = rw;
    c.height = rh;
    const g = c.getContext("2d");
    g.translate(rw / 2, rh / 2);
    g.rotate((rot * Math.PI) / 180);
    g.drawImage(rawImg, -rawImg.naturalWidth / 2, -rawImg.naturalHeight / 2);
    $("raw").src = rot ? c.toDataURL("image/jpeg", 0.9) : rawImg.src;
}
// Snap distances are measured in CSS pixels, so the feel stays consistent
// across preview sizes. A wider release radius avoids jitter near a guide.
const SNAP_ENTER_PX = 6,
    SNAP_RELEASE_PX = 10;
const SNAP_GUIDES = {
    x: ["snap-left", "snap-center-x", "snap-right"],
    y: ["snap-top", "snap-center-y", "snap-bottom"]
};
function clearSnapGuides() {
    for (const ids of Object.values(SNAP_GUIDES)) {
        for (const id of ids) $(id).hidden = true;
    }
}
function showSnapGuides(x, y) {
    clearSnapGuides();
    for (const [axis, match] of [
        ["x", x],
        ["y", y]
    ]) {
        if (match) $(SNAP_GUIDES[axis][match.index]).hidden = false;
    }
}
function snapAxis(position, imageSize, frameSize, cssScale, previous = null) {
    const targets = [0, (frameSize - imageSize) / 2, frameSize - imageSize];
    if (previous) {
        const value = targets[previous.index];
        if (Math.abs(position - value) * cssScale <= SNAP_RELEASE_PX) {
            return { value, index: previous.index };
        }
    }
    let nearest = null,
        distance = SNAP_ENTER_PX;
    // Prefer the center guide when equal-sized image/frame edges coincide.
    for (const index of [1, 0, 2]) {
        const delta = Math.abs(position - targets[index]) * cssScale;
        if (delta <= distance && (!nearest || delta < distance)) {
            nearest = { value: targets[index], index };
            distance = delta;
        }
    }
    return nearest;
}
function layout() {
    if (!rawImg || !$("frame").clientWidth) return;
    const k = $("frame").clientWidth / PW,
        r = $("raw");
    const x = Math.round(ox),
        y = Math.round(oy),
        w = Math.max(1, Math.round(rw * sc)),
        h = Math.max(1, Math.round(rh * sc));
    Object.assign(r.style, {
        left: x * k + "px",
        top: y * k + "px",
        width: w * k + "px",
        height: h * k + "px"
    });
    const z = sc / fillScale();
    $("zs").value = Math.round(Math.log(z) * 100);
    $("zpct").disabled = false;
    $("zpct").value = Number((z * 100).toFixed(2));
}
let ctimer = 0;
function changed(keepGuides = false) {
    if (!keepGuides) clearSnapGuides();
    layout();
    invalidate();
    clearTimeout(ctimer);
    ctimer = setTimeout(() => {
        savePlace();
        convert();
    }, 350);
}
function savePlace() {
    if (!ikey) return;
    const v = { rot, scale: sc, ox, oy };
    settings.placements = settings.placements || {};
    settings.placements[ikey] = v;
    api("/api/settings", { placements: { [ikey]: v } }).catch(() => {});
}
function zoomAt(f, px, py) {
    const s2 = Math.min(Math.max(sc * f, fillScale() * 0.1), fillScale() * 20);
    f = s2 / sc;
    ox = px - (px - ox) * f;
    oy = py - (py - oy) * f;
    sc = s2;
    changed();
}
(() => {
    const e = $("edit");
    let d = null;
    const move = (ev) => {
        if (!d || ev.pointerId !== d.pointerId) return;
        const k = $("frame").clientWidth / PW;
        if (!k) return;
        d.last = ev;
        const x = d.ox + (ev.clientX - d.x) / k,
            y = d.oy + (ev.clientY - d.y) / k;
        const enabled = $("snap").checked && !ev.altKey;
        d.snapX = enabled ? snapAxis(x, rw * sc, PW, k, d.snapX) : null;
        d.snapY = enabled ? snapAxis(y, rh * sc, PH, k, d.snapY) : null;
        ox = d.snapX ? d.snapX.value : x;
        oy = d.snapY ? d.snapY.value : y;
        showSnapGuides(d.snapX, d.snapY);
        layout();
    };
    e.addEventListener("pointerdown", (ev) => {
        if (!rawImg || d || ev.button !== 0 || ev.isPrimary === false) return;
        clearTimeout(ctimer);
        clearSnapGuides();
        invalidate();
        d = { x: ev.clientX, y: ev.clientY, ox, oy, pointerId: ev.pointerId };
        e.setPointerCapture(ev.pointerId);
        e.classList.add("drag");
    });
    e.addEventListener("pointermove", move);
    const up = (ev) => {
        if (!d || ev.pointerId !== d.pointerId) return;
        if (ev.type === "pointerup") move(ev);
        const pointerId = d.pointerId;
        d = null;
        if (e.hasPointerCapture(pointerId)) e.releasePointerCapture(pointerId);
        e.classList.remove("drag");
        changed(true);
    };
    e.addEventListener("pointerup", up);
    e.addEventListener("pointercancel", up);
    e.addEventListener("lostpointercapture", up);
    $("snap").onchange = () => {
        clearSnapGuides();
        if (d && d.last) move(d.last);
    };
    // Alt / Option also works without an extra pointer movement.
    for (const type of ["keydown", "keyup"]) {
        document.addEventListener(type, (ev) => {
            if (ev.key !== "Alt" || !d || !d.last) return;
            move({
                pointerId: d.pointerId,
                clientX: d.last.clientX,
                clientY: d.last.clientY,
                altKey: type === "keydown"
            });
        });
    }
    e.addEventListener(
        "wheel",
        (ev) => {
            if (!rawImg || d) return;
            ev.preventDefault();
            const b = $("frame").getBoundingClientRect(),
                k = b.width / PW;
            zoomAt(
                Math.exp(-ev.deltaY * 0.0015),
                (ev.clientX - b.left) / k,
                (ev.clientY - b.top) / k
            );
        },
        { passive: false }
    );
})();
$("zs").oninput = () => {
    if (!rawImg) return;
    zoomAt((fillScale() * Math.exp($("zs").value / 100)) / sc, PW / 2, PH / 2);
};
$("zpct").onchange = () => {
    if (!rawImg) return;
    const pct = Number($("zpct").value);
    if (!Number.isFinite(pct) || pct <= 0) {
        layout();
        return;
    }
    zoomAt((fillScale() * pct) / 100 / sc, PW / 2, PH / 2);
};
$("zpct").onkeydown = (ev) => {
    if (ev.key === "Enter") $("zpct").blur();
};
$("bfill").onclick = () => {
    if (rawImg) {
        preset("crop");
        changed();
    }
};
$("bfit").onclick = () => {
    if (rawImg) {
        preset("pad");
        changed();
    }
};
$("breset").onclick = () => {
    if (rawImg) {
        rot = 0;
        rotImage();
        preset("crop");
        changed();
    }
};
$("brot").onclick = () => {
    if (!rawImg) return;
    const cx = (PW / 2 - ox) / sc,
        cy = (PH / 2 - oy) / sc,
        [w0, h0] = [rw, rh]; /* keep the frame centre on the same picture point */
    rot = (rot + 90) % 360;
    rotImage();
    const nx = h0 - cy,
        ny = cx;
    ox = PW / 2 - nx * sc;
    oy = PH / 2 - ny * sc;
    changed();
};
window.addEventListener("resize", layout);
$("showd").onchange = () => ($("dimg").style.visibility = $("showd").checked ? "" : "hidden");
async function imgKey(dataUrl, f) {
    try {
        const h = await crypto.subtle.digest("SHA-1", new TextEncoder().encode(dataUrl));
        return [...new Uint8Array(h)].map((b) => b.toString(16).padStart(2, "0")).join("");
    } catch (e) {
        return f.name + ":" + f.size + ":" + f.lastModified;
    }
}
let cseq = 0,
    converting = false,
    pendingConversion = false,
    loading = false,
    loadseq = 0;
function invalidate() {
    ++cseq;
    cur = null;
    syncControls();
    $("dimg").classList.add("stale");
}
async function convert() {
    if (!src || loading) return;
    invalidate();
    if (converting) {
        pendingConversion = true;
        return;
    }
    converting = true;
    const my = cseq;
    msg("");
    $("hint").textContent =
        $("mode").value === "fine" ? "精细转换中，请稍候（首次使用需要准备计算组件）…" : "转换中…";
    try {
        const j = await api("/api/convert", {
            image: src,
            filename: fname,
            mode: $("mode").value,
            place: { rotate: (360 - rot) % 360, scale: sc, ox, oy }
        });
        if (my !== cseq) return; /* a newer conversion is in flight */
        cur = j.id;
        $("dimg").src = j.preview;
        $("dimg").classList.remove("stale");
        setPane($("prev"), j.preview);
        if (!$("name").dataset.touched) $("name").value = j.name;
        syncControls();
        $("hint").textContent =
            j.coverage < 0.75 ? "留白较多，画面会显得很小：可以点「铺满」或「旋转 90°」。" : "";
    } catch (e) {
        if (my === cseq) {
            $("hint").textContent = "";
            msg(e.message);
        }
    } finally {
        converting = false;
        if (pendingConversion) {
            pendingConversion = false;
            convert();
        }
    }
}
function load(f) {
    if (!f) return;
    if (!f.type.startsWith("image/")) return msg("请拖入图片文件（JPG/PNG 等）");
    const mine = ++loadseq;
    loading = true;
    clearSnapGuides();
    invalidate();
    clearTimeout(ctimer);
    $("name").dataset.touched = "";
    const fail = () => {
        if (mine === loadseq) {
            loading = false;
            src = null;
            msg("无法识别这个图片文件");
        }
    };
    const r = new FileReader();
    r.onerror = fail;
    r.onload = async () => {
        const data = r.result,
            key = await imgKey(data, f);
        if (mine !== loadseq) return;
        const im = new Image();
        im.onload = () => {
            if (mine !== loadseq) return;
            loading = false;
            src = data;
            fname = f.name;
            ikey = key;
            rawImg = im;
            $("ph").style.display = "none";
            const p = (settings.placements || {})[ikey];
            if (p && p.scale > 0) {
                rot = p.rot || 0;
                rotImage();
                sc = p.scale;
                ox = p.ox;
                oy = p.oy;
            } else {
                rot = 0;
                rotImage();
                preset("crop");
            }
            layout();
            $("dimg").classList.add("stale");
            convert();
        };
        im.onerror = fail;
        im.src = data;
    };
    r.readAsDataURL(f);
}
$("drop").onclick = () => $("file").click();
$("file").onchange = (e) => load(e.target.files[0]);
["dragover", "dragenter"].forEach((ev) =>
    $("drop").addEventListener(ev, (e) => {
        e.preventDefault();
        $("drop").classList.add("over");
    })
);
["dragleave", "drop"].forEach((ev) =>
    $("drop").addEventListener(ev, (e) => {
        e.preventDefault();
        $("drop").classList.remove("over");
    })
);
document.addEventListener("dragover", (e) => e.preventDefault());
document.addEventListener("drop", (e) => {
    e.preventDefault();
    load(e.dataTransfer.files[0]);
});
$("mode").onchange = convert;
$("name").oninput = () => ($("name").dataset.touched = 1);
$("rescan").onclick = () => ports(true);
let zmode = "mm";
function zoom(z) {
    if (z === "0" || z === 0) {
        $("zoom").classList.remove("on");
        $("zoom").hidden = true;
        return;
    }
    if (z === "cal") return openCal();
    zmode = z;
    let w, h, cap;
    if (z === "mm") {
        w = PMM[0] * pxmm;
        h = PMM[1] * pxmm;
        cap = `实际尺寸 ${PMM[0].toFixed(1)} × ${PMM[1].toFixed(1)} mm（不准请点「校准尺寸」）`;
    } else if (z === "px") {
        w = PW / devicePixelRatio;
        h = PH / devicePixelRatio;
        cap = "像素 1:1：一个屏幕像素 = 相框一个像素";
    } else {
        w = PW * 2;
        h = PH * 2;
        cap = "2× 放大";
    }
    $("zimg").style.width = w + "px";
    $("zimg").style.height = h + "px";
    $("zcap").textContent = cap;
    $("zoom").hidden = false;
    $("zoom").classList.add("on");
}
$("prev").onclick = () => {
    if (!cur) return;
    $("zimg").src = $("prev").src;
    zoom("mm");
};
$("zoom").onclick = (e) => {
    const z = e.target.dataset && e.target.dataset.z;
    if (z !== undefined) zoom(z);
    else if (e.target.id === "zoom") zoom(0);
    else zoom(zmode === "mm" ? "px" : "mm");
};
function calShow(v) {
    $("card").style.width = 85.6 * v + "px";
    $("card").style.height = 53.98 * v + "px";
    $("calv").textContent = (v * 25.4).toFixed(0) + " px/英寸";
}
function openCal() {
    $("calr").value = pxmm;
    calShow(pxmm);
    $("calib").hidden = false;
    $("calib").classList.add("on");
}
$("calr").oninput = () => calShow(+$("calr").value);
$("calreset").onclick = () => {
    $("calr").value = 96 / 25.4;
    calShow(96 / 25.4);
};
function closeCal() {
    $("calib").classList.remove("on");
    $("calib").hidden = true;
}
$("calno").onclick = closeCal;
$("calok").onclick = () => {
    pxmm = +$("calr").value;
    settings.px_per_mm = pxmm;
    api("/api/settings", { px_per_mm: pxmm }).catch((e) => msg(e.message));
    closeCal();
    if ($("zoom").classList.contains("on")) zoom(zmode);
};
api("/api/settings")
    .then((j) => {
        settings = j.settings || {};
        PMM = j.panel_mm;
        if (settings.px_per_mm > 0) pxmm = settings.px_per_mm;
    })
    .catch(() => {});
document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
        zoom(0);
        closeCal();
    }
});
// Silent uploads and the explicit batch-session finish action.
$("send").onclick = async () => {
    const port = $("port").value,
        bar = $("bar");
    if (!port) return msg(NOPORT);
    if ($("send").disabled || busy()) return;
    sending = true;
    ++refreshSeq;
    syncControls();
    msg("");
    bar.classList.remove("failed");
    bar.value = 0;
    $("pct").textContent = "连接中…";
    try {
        const { job } = await api("/api/send", {
            id: cur,
            name: $("name").value,
            port,
            show: false
        });
        batchActive = true;
        syncControls();
        for (;;) {
            const j = await api("/api/job?id=" + job);
            bar.value = j.done / j.total;
            $("pct").textContent =
                j.done === j.total ? "校验并保存中…" : Math.floor((100 * j.done) / j.total) + "%";
            if (j.state === "ok") {
                $("pct").textContent = "✓ 已保存，可继续发送下一张";
                break;
            }
            if (j.state === "error") throw new Error(j.error);
            await new Promise((r) => setTimeout(r, 250));
        }
    } catch (e) {
        bar.classList.add("failed");
        bar.value = 0;
        $("pct").textContent = "✗ 发送失败";
        msg(e.message);
    } finally {
        sending = false;
        syncControls();
        await refresh();
    }
};
$("finish").onclick = async () => {
    if (busy() || $("finish").disabled) return;
    finishing = true;
    ++refreshSeq;
    syncControls();
    msg("");
    try {
        await api("/api/session", { port: $("port").value, action: 0 });
        batchActive = false;
        if (deviceInfo)
            deviceInfo = {
                ...deviceInfo,
                upload_session_active: false,
                state: deviceInfo.state === "uploading" ? "idle" : deviceInfo.state
            };
        $("pct").textContent = "✓ 传图已结束，画面保持原样";
        setTimeout(refresh, 1000);
    } catch (e) {
        msg("结束传图未确认，请重试。" + e.message);
    } finally {
        finishing = false;
        syncControls();
    }
};
async function keepalive() {
    if (
        !batchActive ||
        busy() ||
        keepingAlive ||
        !deviceInfo ||
        !deviceInfo.upload_session_supported
    )
        return;
    keepingAlive = true;
    syncControls();
    try {
        const j = await api("/api/session", { port: $("port").value, action: 2 });
        if (!j.active) {
            batchActive = false;
            deviceInfo = {
                ...deviceInfo,
                upload_session_active: false,
                state: deviceInfo.state === "uploading" ? "idle" : deviceInfo.state
            };
            msg("传图会话已结束；下一次发送会重新开始。", "info");
        }
    } catch (e) {
        batchActive = false;
        connected = false;
        deviceInfo = null;
        msg("连接中断，请重新检测。相框会在停止传输后最多两分钟恢复按键。" + e.message);
    } finally {
        keepingAlive = false;
        syncControls();
    }
}
setInterval(keepalive, 10000);
// Gallery rows and independent ordering / playback-selection drafts.
function empty(text, cls) {
    $("cards").innerHTML = "";
    const li = document.createElement("li");
    li.className = "empty " + (cls || "");
    li.textContent = text;
    $("cards").appendChild(li);
}
function draw() {
    const ul = $("cards");
    if (!items.length) {
        empty("卡上还没有图片");
        syncControls();
        return;
    }
    ul.innerHTML = "";
    items.forEach((it, i) => {
        const li = document.createElement("li");
        li.innerHTML =
            "<span class=n></span><span class=t></span><button title='上移'>↑</button><button title='下移'>↓</button><button title='在相框上显示'>显示</button><button title='删除'>✕</button><input type='checkbox' title='参与定时轮播' aria-label='参与定时轮播'>";
        const check = li.querySelectorAll("input")[0];
        check.checked = selectionNames.has(it.id);
        check.onchange = () => {
            if (check.disabled) return;
            if (check.checked) selectionNames.add(it.id);
            else selectionNames.delete(it.id);
            selectionDirty = true;
            syncControls();
        };
        li.children[0].textContent = i + 1 + ".";
        const savedNext = nextSavedPicture();
        const badges = [];
        if (deviceInfo && it.id === deviceInfo.name) badges.push("当前显示");
        if (savedNext && savedNext.id === it.id && it.id !== deviceInfo.name) badges.push("计划下一张");
        li.children[1].textContent = it.label + (badges.length ? " · " + badges.join(" · ") : "");
        li.children[1].title = it.label;
        const b = li.querySelectorAll("button");
        const mv = (d) => {
            const k = i + d;
            if (busy() || batchActive || deviceBusy() || k < 0 || k >= items.length) return;
            [items[k], items[i]] = [items[i], items[k]];
            orderDirty = true;
            draw();
            syncControls();
        };
        b[0].onclick = () => mv(-1);
        b[1].onclick = () => mv(1);
        b[2].onclick = () =>
            api("/api/show", { port: $("port").value, name: it.id })
                .then(() => { msg("正在切换照片，请等待屏幕刷新完成。", "success"); pollPlayback(); })
                .catch((e) => msg(e.message));
        b[3].onclick = async () => {
            if (!confirm("从相框删除「" + it.label + "」？")) return;
            try {
                await api("/api/delete", { port: $("port").value, name: it.id });
                refresh();
            } catch (e) {
                msg(e.message);
            }
        };
        ul.appendChild(li);
    });
    syncControls();
}
async function refresh() {
    if (busy()) return;
    const mine = ++refreshSeq,
        port = $("port").value;
    if (port && port !== slideshowPort) {
        editingSlideshow = false;
        selectionDirty = orderDirty = planConflict = false;
        selectionNames = new Set();
        slideshowPort = port;
    }
    connected = false;
    syncControls();
    if (!port) {
        if (!planDirty()) items = [];
        syncControls();
        return empty("未连接设备，已保留最后确认的计划与未保存修改");
    }
    empty("读取中…");
    try {
        const i = await api("/api/info?port=" + encodeURIComponent(port));
        if (mine !== refreshSeq || busy()) return;
        deviceInfo = i;
        infoSeenAt = Date.now();
        connected = true;
        if (i.upload_session_supported) batchActive = !!i.upload_session_active;
        syncControls();
        if (i.ready === false || ["refreshing", "maintenance"].includes(i.state)) {
            empty(i.ready === false ? "设备正在启动…" : "相框正在刷新或维护…");
            setTimeout(refresh, 1000);
            return;
        }
        if (i.state === "fault") return empty("TF 卡故障，请检查卡并重启相框", "err");
        if (!i.upload_no_refresh) msg("设备固件不支持静默上传，请先升级到 1.4.0 或更新版本");
        const j = await api("/api/list?port=" + encodeURIComponent(port));
        if (mine !== refreshSeq || busy()) return;
        connected = j.connected;
        const changed = selectionMeta && j.selection && selectionMeta.token !== j.selection.token;
        if (planDirty() && changed) planConflict = true;
        savedItems = j.items.map(it => ({...it, selected: typeof it.selected === "boolean" ? it.selected : !j.selection}));
        if (!orderDirty && !planConflict) items = savedItems.map(it => ({...it}));
        selectionMeta = j.selection || null;
        planStateUnknown = false;
        if (!selectionDirty)
            selectionNames = new Set(items.filter((it) => it.selected).map((it) => it.id));
        else
            selectionNames = new Set(
                items.filter((it) => selectionNames.has(it.id)).map((it) => it.id)
            );
        draw();
        if (i.storage_cache_valid === false && !batchActive) setTimeout(refresh, 1000);
    } catch (e) {
        if (mine !== refreshSeq || busy()) return;
        if (e.deviceStatus === -9) {
            if (deviceInfo) deviceInfo = { ...deviceInfo, state: "maintenance" };
            syncControls();
            empty("设备忙碌，稍后自动更新…");
            setTimeout(refresh, 1000);
            return;
        }
        connected = false;
        batchActive = false;
        syncControls();
        empty("读取失败：" + e.message, "err");
    }
}
$("refresh").onclick = async () => {
    if (planDirty() && !confirm("重新读取将放弃尚未保存的修改，继续？")) return;
    editingSlideshow = selectionDirty = orderDirty = planConflict = false;
    await refresh();
};
$("port").onchange = async () => {
    if (planDirty() && !confirm("切换设备将放弃尚未保存的修改，继续？")) {
        $("port").value = slideshowPort; return;
    }
    editingSlideshow = selectionDirty = orderDirty = planConflict = false;
    savedItems = []; deviceInfo = null; selectionMeta = null;
    await refresh();
};
$("fullimage").onchange = async () => {
    if (busy() || batchActive || $("fullimage").disabled) return;
    const port = $("port").value,
        on = !$("fullimage").checked,
        mine = ++refreshSeq;
    changingDisplay = true;
    syncControls();
    msg("");
    try {
        await api("/api/strip", { port, on });
        if (mine === refreshSeq) deviceInfo = { ...deviceInfo, strip: on };
    } catch (e) {
        msg(e.message);
        try {
            const i = await api("/api/info?port=" + encodeURIComponent(port));
            if (mine === refreshSeq) {
                deviceInfo = i;
                infoSeenAt = Date.now();
                connected = true;
            }
        } catch (e) {
            if (mine === refreshSeq) {
                connected = false;
                deviceInfo = null;
            }
        }
    } finally {
        changingDisplay = false;
        syncControls();
    }
};
$("slideshow").onchange =
    $("slideinterval").oninput =
    $("tapwake").onchange =
        () => {
            editingSlideshow =
                !deviceInfo ||
                $("slideshow").checked !== deviceInfo.slideshow_enabled ||
                Number($("slideinterval").value) !== deviceInfo.slideshow_interval_min ||
                (deviceInfo.tap_wake_supported === true &&
                    !deviceInfo.remote_sleep_managed && $("tapwake").checked !== deviceInfo.tap_wake);
            syncControls();
        };
function selectPictures(all) {
    if ($(all ? "selectall" : "selectnone").disabled) return;
    selectionNames = new Set(all ? items.map(it => it.id) : []);
    selectionDirty = true;
    draw();
}
$("selectall").onclick = () => selectPictures(true);
$("selectnone").onclick = () => selectPictures(false);
$("saveplan").onclick = async () => {
    if ($("saveplan").disabled || busy()) return;
    const body = {port: $("port").value, token: selectionMeta && selectionMeta.token};
    if (orderDirty) body.order = items.map(it => it.id);
    if (selectionDirty) body.names = items.filter(it => selectionNames.has(it.id)).map(it => it.id);
    if (editingSlideshow) {
        const interval = Number($("slideinterval").value);
        if (!Number.isInteger(interval) || interval < 1 || interval > 10080)
            return msg("换图间隔必须是1～10080的整数分钟");
        body.settings = {enabled: $("slideshow").checked, interval_min: interval};
        if (deviceInfo.tap_wake_supported && !deviceInfo.remote_sleep_managed)
            body.settings.tap_wake = $("tapwake").checked;
    }
    changingSelection = true; ++refreshSeq; syncControls(); msg("");
    try {
        const j = await api("/api/slideshow-plan", body);
        const confirmed = j.confirmed || [];
        if (confirmed.includes("order")) orderDirty = false;
        if (confirmed.includes("selection")) selectionDirty = false;
        if (confirmed.includes("settings")) editingSlideshow = false;
        planStateUnknown = !!j.state_unknown;
        if (j.info) { deviceInfo = j.info; infoSeenAt = Date.now(); }
        const labels = new Map([...savedItems, ...items].map(it => [it.id, it.label]));
        if (j.selection) {
            selectionMeta = {...j.selection};
            savedItems = j.selection.items.map(it => ({...it, label: labels.get(it.id) || it.id}));
        } else if (j.order) savedItems = j.order.map(id => ({...(savedItems.find(it => it.id === id) || {id}), label: labels.get(id) || id}));
        if (!orderDirty) items = savedItems.map(it => ({...it}));
        if (!selectionDirty) selectionNames = new Set(savedItems.filter(it => it.selected).map(it => it.id));
        planConflict = j.failed === "read";
        const stageNames = {order:"图库顺序", selection:"轮播名单", settings:"定时设置", read:"读取设备"};
        msg(j.ok ? "所有更改已保存并读回确认。" :
            `${confirmed.length ? "已保存：" + confirmed.map(k => stageNames[k]).join("、") + "。" : ""}${stageNames[j.failed] || "保存"}未确认：${j.error || "请重新读取设备"}。未完成的修改已保留。`, j.ok ? "success" : "error");
    } catch (e) {
        planStateUnknown = true;
        msg("保存结果未确认，修改已保留。请重新读取设备核对。" + e.message);
    } finally { changingSelection = false; draw(); }
};
let pollingPlayback = false;
async function pollPlayback() {
    if (pollingPlayback || busy() || batchActive || !connected || document.hidden) return;
    const port = $("port").value, generation = refreshSeq;
    pollingPlayback = true;
    try {
        const i = await api("/api/info?port=" + encodeURIComponent(port));
        if (port !== $("port").value || generation !== refreshSeq || busy()) return;
        const countChanged = deviceInfo && i.n !== deviceInfo.n;
        deviceInfo = i; infoSeenAt = Date.now();
        if (countChanged) await refresh(); else draw();
    } catch (e) { if (generation === refreshSeq) { connected = false; syncControls(); } }
    finally { pollingPlayback = false; }
}
setInterval(pollPlayback, 2000);
window.addEventListener("beforeunload", (e) => {
    if (planDirty()) {
        e.preventDefault();
        e.returnValue = "";
    }
});
ports(true);
setInterval(() => ports(false), 3000);
