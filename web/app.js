async function postJSON(url, body) {
  let res;

  try {
    res = await fetch(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body || {}),
    });
  } catch (_) { return { _status: 0 }; }
  let data = {}; try { data = await res.json(); } catch (_) {}
  data._status = res.status; return data;
}
async function getJSON(url) {
  let res;
  try { res = await fetch(url); } catch (_) { return { _status: 0 }; }
  let data = {}; try { data = await res.json(); } catch (_) {}
  data._status = res.status; return data;
}
const $ = (id) => document.getElementById(id);

const cssVar = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

function currentTheme() {
  return document.documentElement.getAttribute("data-theme") === "dark" ? "dark" : "light";
}
function applyThemeIcon() {

  $("themeToggle").textContent = currentTheme() === "dark" ? "☀" : "🌙";
}
function toggleTheme() {
  const next = currentTheme() === "dark" ? "light" : "dark";
  document.documentElement.setAttribute("data-theme", next);
  try { localStorage.setItem("tf-theme", next); } catch (e) {}
  applyThemeIcon();
  if (CURRENT_VIEW === "stats") refreshStats();
}

let ME = null;
let CURRENT_VIEW = "contest";
let STATE = null;
let SELECTED_ID = null;
let CONTEST_MODE = "list";
let clockOffset = 0;
let pollTimer = null, tickTimer = null;
let MY_LOGGED = new Set();

function setView(view) {
  CURRENT_VIEW = view;
  document.querySelectorAll(".view").forEach((v) => v.classList.add("hidden"));
  const el = $("view-" + view);
  if (el) el.classList.remove("hidden");
  document.querySelectorAll(".tab").forEach((t) =>
    t.classList.toggle("active", t.dataset.view === view));
  if (view === "contest") enterContestView();
  if (view === "upsolve") refreshUpsolve();
  if (view === "logs") refreshLogs();
  if (view === "stats") refreshStats();
  if (view === "rules") loadRules();
}

function renderAuth() {
  const inn = !!ME;
  $("loginCard").classList.toggle("hidden", inn);
  $("tabs").classList.toggle("hidden", !inn);
  $("logoutBtn").classList.toggle("hidden", !inn);
  $("adminTab").classList.toggle("hidden", !(inn && ME.role === "admin"));
  $("who").textContent = inn ? `${ME.username} (${ME.role})` : "not logged in";
  if (inn) setView("contest"); else showLoginOnly();
}
function showLoginOnly() {
  document.querySelectorAll(".view").forEach((v) => v.classList.add("hidden"));
}

async function refreshWhoAmI() {
  const me = await getJSON("/api/whoami");
  ME = me._status === 200 ? { username: me.username, role: me.role, cf_handle: me.cf_handle } : null;
  if (ME) { await refreshBookmarks(); await refreshMyLogged(); }
  renderAuth();
}

async function refreshMyLogged() {
  const d = await getJSON("/api/logs?user=" + encodeURIComponent(ME.username));
  MY_LOGGED = new Set((d.logs || []).map((l) => l.problem_key));
}

let BOOKMARKS = new Set();
async function refreshBookmarks() {
  const d = await getJSON("/api/bookmarks");
  BOOKMARKS = new Set((d.items || []).map((i) => i.key));
  return d;
}
async function toggleBookmark(key) {
  if (BOOKMARKS.has(key)) {
    await fetch("/api/bookmark", { method: "DELETE",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ problem_key: key }) });
    BOOKMARKS.delete(key);
  } else {
    await postJSON("/api/bookmark", { problem_key: key });
    BOOKMARKS.add(key);
  }
  if (CURRENT_VIEW === "contest") renderContest();
  else if (CURRENT_VIEW === "stats") renderBookmarks();
}
window.toggleBookmark = toggleBookmark;

async function enterContestView() {
  const d = await getJSON("/api/contests");
  const items = (d && d.items) || [];
  const running = items.find((c) => c.status === "live" || c.status === "paused");
  if (running) openContest(running.id);
  else showContestList();
}

function showContestList() {
  CONTEST_MODE = "list";
  SELECTED_ID = null;
  $("archiveCard").classList.remove("hidden");
  $("backToList").classList.add("hidden");
  $("contestHeader").classList.add("hidden");
  $("problemsCard").classList.add("hidden");
  $("resultsCard").classList.add("hidden");
  refreshArchive();
}

function openContest(id) {
  CONTEST_MODE = "detail";
  SELECTED_ID = id;
  $("archiveCard").classList.add("hidden");
  $("backToList").classList.remove("hidden");
  $("contestHeader").classList.remove("hidden");
  refreshState();
  window.scrollTo(0, 0);
}

async function refreshState() {
  const url = SELECTED_ID ? "/api/state?id=" + encodeURIComponent(SELECTED_ID) : "/api/state";
  const s = await getJSON(url);
  if (s._status !== 200) return;
  STATE = s;
  clockOffset = s.server_now - Math.floor(Date.now() / 1000);
  renderContest();
}

async function refreshArchive() {
  const d = await getJSON("/api/contests");
  const box = $("contestArchive");
  const items = d.items || [];
  if (!items.length) { box.innerHTML = `<p class="hint">No contests yet.</p>`; return; }
  const openId = (STATE && STATE.contest) ? STATE.contest.id : null;
  box.innerHTML = items.map((c) => {
    const live = c.status === "live" || c.status === "paused";
    const cls = "round rs-" + c.status + (live ? " livecard" : "") + (c.id === openId ? " selected" : "");
    const when = c.status === "scheduled"
      ? "starts " + fmtShort(c.scheduled_at)
      : (c.start ? fmtShort(c.start) : "not started");

    const parts = String(c.name).split("#");
    const prefix = parts.length > 1 ? parts[0] + "#" : "";
    const num = parts.length > 1 ? parts[1] : c.name;
    const dot = live ? '<span class="livedot"></span>' : "";
    return `<div class="${cls}" onclick="selectContest('${c.id}')" title="Open ${c.name}">
      <div class="round-top">
        <span class="round-no"><span class="rp">${prefix}</span><span class="rn">${num}</span></span>
        <span class="rchip">${dot}${c.status}</span>
      </div>
      <div class="round-meta">${when}</div>
      <div class="round-open">Open →</div>
    </div>`;
  }).join("");
}
function selectContest(id) { openContest(id); }
window.selectContest = selectContest;
window.showContestList = showContestList;

function renderContest() {
  const c = STATE && STATE.contest;
  const title = $("contestTitle"), body = $("contestBody");
  const probCard = $("problemsCard"), resCard = $("resultsCard");
  const cname = c ? c.name : "";

  if (!c) {
    title.textContent = SELECTED_ID ? "Contest not available" : "No contest yet";
    body.innerHTML = ME.role === "admin"
      ? `<p class="hint">Create one from the <b>Admin</b> tab.</p>`
      : `<p class="hint">Sit tight — no contest is running.</p>`;
    probCard.classList.add("hidden"); resCard.classList.add("hidden");
    $("timer").textContent = "";
    renderAdminControls(null);
    return;
  }

  if (c.status === "draft" || c.status === "scheduled") {
    const badge = c.status === "scheduled"
      ? '<span class="badge">scheduled</span>' : '<span class="badge">draft</span>';
    title.innerHTML = `${cname} ${badge}`;
    const sched = c.status === "scheduled"
      ? `<p class="hint">Opens automatically at <b>${fmtDate(c.scheduled_at)}</b>.</p>` : "";
    body.innerHTML = ME.role === "admin"
      ? `<p>${c.problem_count} problems selected (hidden).</p>${sched}`
      : `<p class="hint">A contest is being prepared. It'll appear the moment it starts.</p>${sched}`;
    probCard.classList.add("hidden"); resCard.classList.add("hidden");
    renderAdminControls(c);
    updateTimer();
    return;
  }

  const badge = { live: '<span class="badge live">live</span>',
                  paused: '<span class="badge paused">paused</span>',
                  finished: '<span class="badge">finished</span>' }[c.status] || "";
  title.innerHTML = `${cname} ${badge}`;
  const started = c.start ? fmtShort(c.start) : "—";
  body.innerHTML =
    `<div class="cmeta">
       <div class="cmeta-item"><span class="cmeta-k">Duration</span><span class="cmeta-v">${c.duration_min} min</span></div>
       <div class="cmeta-item"><span class="cmeta-k">Problems</span><span class="cmeta-v">${c.problems.length}</span></div>
       <div class="cmeta-item"><span class="cmeta-k">Started</span><span class="cmeta-v">${started}</span></div>
     </div>` +
    (c.status === "paused" ? `<p class="hint">Paused by the admin.</p>` : "");

  renderProblems(c);
  renderResults(c);
  probCard.classList.remove("hidden");
  resCard.classList.remove("hidden");
  renderAdminControls(c);
  updateTimer();
}

function renderAdminControls(c) {
  const el = $("adminControls");
  if (!ME || ME.role !== "admin" || !c) { el.classList.add("hidden"); el.innerHTML = ""; return; }
  const id = c.id;
  let btns = "";
  if (c.status === "draft" || c.status === "scheduled")
    btns += `<button onclick="contestAction('start','${id}')">Start now</button>`;
  else if (c.status === "live")   btns += `<button class="warn" onclick="contestAction('pause','${id}')">Pause</button>`;
  else if (c.status === "paused") btns += `<button onclick="contestAction('resume','${id}')">Resume</button>`;

  if (c.status === "live" || c.status === "paused")
    btns += `<button onclick="contestAction('finish','${id}')">Finish now</button>`;
  btns += `<button class="danger" onclick="contestAction('delete_contest','${id}')">Delete</button>`;
  el.innerHTML = btns;
  el.classList.remove("hidden");
}

async function contestAction(action, id) {
  const msg = $("controlMsg"); msg.className = "msg";
  if (action === "delete_contest" &&
      !confirm("Delete this contest? Its results and upsolve queues are removed.")) return;
  if (action === "finish" &&
      !confirm("Finish this contest now? The clock stops here, unsolved problems go to "
             + "everyone's upsolve queue, and logs unlock. This can't be undone.")) return;
  const r = await postJSON("/api/admin/" + action, { id });
  if (r.ok) {
    if (action === "delete_contest") { showContestList(); }
    else { msg.className = "msg ok"; msg.textContent = "Done."; await refreshState(); }
  } else { msg.className = "msg bad"; msg.textContent = r.error || "action failed"; }
}
window.contestAction = contestAction;

function renderProblems(c) {
  const ol = $("problemList");
  ol.innerHTML = "";
  const finished = c.status === "finished";
  c.problems.forEach((p) => {
    const li = document.createElement("li");

    const on = BOOKMARKS.has(p.key);
    const logged = MY_LOGGED.has(p.key);

    const logBtn = finished
      ? `<button class="pbtn ${logged ? "logged" : ""}" title="add / edit your log"
           onclick="openLogForm('${p.key}','${c.id}')">${logged ? "✓ logged" : "＋ log"}</button>` : "";
    const timesBtn = `<button class="pbtn" title="see everyone's time + logs"
           onclick="openTimesLogs('${p.key}')">times &amp; logs</button>`;
    li.innerHTML =
      `<a href="${p.url}" target="_blank" rel="noopener">${p.index} — ${escapeHtml(p.name)}</a>` +
      (p.rating ? `<span class="rating-pill">${p.rating}</span>` : "") +
      `<button class="star ${on ? "on" : ""}" title="bookmark (private)"
        onclick="toggleBookmark('${p.key}')">${on ? "★" : "☆"}</button>` +
      `<span class="pbtns">${logBtn}${timesBtn}</span>`;
    ol.appendChild(li);
  });
}

function renderResults(c) {
  const t = $("resultsTable");
  const probs = c.problems;

  let html = "<thead><tr><th class='member'>Member</th>";
  probs.forEach((p, i) => {
    html += `<th><a href="${p.url}" target="_blank" rel="noopener"
             style="color:inherit;text-decoration:none">Q${i + 1}</a></th>`;
  });
  html += "</tr></thead><tbody>";

  c.members.forEach((m) => {
    const meRow = m.username === ME.username;
    html += `<tr><td class="member ${meRow ? "me" : ""}">${escapeHtml(m.username)}</td>`;
    probs.forEach((p) => {
      html += "<td>" + cellHtml(m.results[p.key]) + "</td>";
    });
    html += "</tr>";
  });
  html += "</tbody>";
  t.innerHTML = html;
}

function cellHtml(r) {
  if (!r) return `<span class="cell-none">–</span>`;
  if (r.solved) {
    const up = r.upsolved ? ` <span class="up" title="upsolved">↑</span>` : "";
    const wrong = r.wrong_attempts > 0 ? ` <span class="cell-wrong">−${r.wrong_attempts}</span>` : "";

    const eff = (r.effort_min != null && r.effort_min >= 0)
      ? ` <span class="effort" title="time actually spent on this problem">(${r.effort_min}′)</span>` : "";
    return `<span class="cell-solved">✓ ${r.solved_at}′</span>${eff}${wrong}${up}`;
  }
  if (r.wrong_attempts > 0) return `<span class="cell-wrong">−${r.wrong_attempts}</span>`;
  return `<span class="cell-none">–</span>`;
}

function updateTimer() {
  const c = STATE && STATE.contest;
  const el = $("timer");
  if (!c || c.status === "draft") { el.textContent = ""; el.className = "timer"; return; }
  if (c.status === "scheduled") {
    const now = Math.floor(Date.now() / 1000) + clockOffset;
    const remain = c.scheduled_at - now;
    if (remain <= 0) { el.textContent = "Starting…"; el.className = "timer"; refreshState(); return; }
    el.textContent = "Starts in " + fmtDuration(remain);
    el.className = "timer paused";
    return;
  }
  if (c.status === "finished") { el.textContent = "Finished"; el.className = "timer ended"; return; }
  if (c.status === "paused") {

    const remain = Math.max(0, c.end - c.paused_at);
    el.textContent = "Paused · " + fmtDuration(remain);
    el.className = "timer paused";
    return;
  }

  const now = Math.floor(Date.now() / 1000) + clockOffset;
  const remain = c.end - now;
  if (remain <= 0) { el.textContent = "Finished"; el.className = "timer ended"; return; }
  el.className = "timer";
  el.textContent = fmtDuration(remain);
}
function fmtDuration(s) {
  const h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = s % 60;
  const pad = (n) => String(n).padStart(2, "0");
  return h > 0 ? `${h}:${pad(m)}:${pad(sec)}` : `${pad(m)}:${pad(sec)}`;
}

function openModal(html) { $("modalBody").innerHTML = html; $("modal").classList.remove("hidden"); }
function closeModal() { $("modal").classList.add("hidden"); $("modalBody").innerHTML = ""; }
window.closeModal = closeModal;

function problemMeta(key) {
  const c = STATE && STATE.contest;
  if (c && c.problems) { const p = c.problems.find((x) => x.key === key); if (p) return p; }
  return { key, index: key, name: "", url: "#" };
}

function openLogForm(key, contestId) {
  const p = problemMeta(key);
  openModal(`
    <h3>Log — ${p.index} ${escapeHtml(p.name)}</h3>
    <label for="m_tag">What happened?</label>
    <select id="m_tag">
      <option value="misread">misread</option>
      <option value="wrong-approach">wrong-approach</option>
      <option value="edge-case">edge-case</option>
      <option value="slow-start">slow-start</option>
      <option value="other">other</option>
    </select>
    <label for="m_text">Notes</label>
    <textarea id="m_text" rows="4" placeholder="What tripped you up? What would you do next time?"></textarea>
    <div class="controls">
      <button onclick="submitLog('${key}','${contestId}')">Save log</button>
      <button class="ghost" onclick="skipProblem('${key}')">No log for this</button>
    </div>
    <div class="msg" id="m_msg"></div>`);
}
window.openLogForm = openLogForm;

async function submitLog(key, contestId) {
  const msg = $("m_msg"); msg.className = "msg";
  const text = $("m_text").value.trim();
  const r = await postJSON("/api/log", { problem_key: key, contest_id: contestId,
    tag: $("m_tag").value, text });
  if (r.ok) {
    MY_LOGGED.add(key); closeModal();
    if (CURRENT_VIEW === "contest") renderContest();
    if (CURRENT_VIEW === "logs") refreshLogs();
  } else { msg.className = "msg bad"; msg.textContent = r.error || "could not save"; }
}
window.submitLog = submitLog;

async function skipProblem(key) {
  await postJSON("/api/log/skip", { problem_key: key });
  closeModal();
  if (CURRENT_VIEW === "logs") refreshLogs();
}
window.skipProblem = skipProblem;

async function openTimesLogs(key) {
  const p = problemMeta(key);
  const c = STATE && STATE.contest;
  let rows = "";
  if (c && c.members) {
    rows = c.members.map((m) => {
      const r = m.results[key];
      let t = "–";
      if (r && r.solved) {
        t = `✓ ${r.solved_at}′` + (r.effort_min >= 0 ? ` (${r.effort_min}′)` : "") +
            (r.upsolved ? " ↑" : "");
      } else if (r && r.wrong_attempts > 0) t = `−${r.wrong_attempts}`;
      const meCls = m.username === ME.username ? ' style="color:var(--accent)"' : "";
      return `<div class="times-row"><span class="who2"${meCls}>${escapeHtml(m.username)}</span><span class="t">${t}</span></div>`;
    }).join("");
  }
  openModal(`<h3>${p.index} ${escapeHtml(p.name)}</h3>
    <div class="sub">Times — ✓ elapsed′ (effort′) · ↑ upsolved</div>${rows || '<p class="hint">No data.</p>'}
    <div class="sub">Logs</div><div id="m_logs"><p class="hint">loading…</p></div>`);
  const d = await getJSON("/api/logs?problem=" + encodeURIComponent(key));
  const logs = d.logs || [];
  $("m_logs").innerHTML = logs.length ? renderLogItems(logs, d.problems || [])
    : '<p class="hint">No logs yet for this problem.</p>';
}
window.openTimesLogs = openTimesLogs;

async function refreshUpsolve() {
  const d = await getJSON("/api/upsolve");
  const box = $("upsolveBody");
  if (d._status !== 200 || !d.items || d.items.length === 0) {
    box.innerHTML = `<p class="hint">Nothing here yet — your queue fills up with
      the problems you don't finish in a contest.</p>`;
    return;
  }
  box.innerHTML = d.items.map((i) => `
    <div class="up-item">
      <a href="${i.url}" target="_blank" rel="noopener">${i.index} — ${escapeHtml(i.name)}</a>
      ${i.done ? '<span class="up-done">✓ done</span>' : '<span class="up-todo">to solve</span>'}
    </div>`).join("");
}

function refreshLogs() {
  renderPending();
  renderMembersGrid();
  $("memberProfileCard").classList.add("hidden");
}

async function renderPending() {
  const d = await getJSON("/api/pending_logs");
  const box = $("pendingList");
  const items = d.items || [];
  if (!items.length) { box.innerHTML = `<p class="hint">All caught up — nothing to log.</p>`; return; }
  box.innerHTML = items.map((i) => {
    const r = i.result;
    let t = "";
    if (r && r.solved) t = `<span class="up-done">✓ ${r.solved_at}′</span>`;
    else if (r) t = `<span class="up-todo">unsolved</span>`;
    return `<div class="pending">
      <a href="${i.url}" target="_blank" rel="noopener">${i.index} — ${escapeHtml(i.name)}</a>
      ${t}<span class="spacer"></span>
      <button class="pbtn" onclick="openLogForm('${i.key}','${i.contest_id}')">＋ log</button>
      <button class="pbtn" onclick="skipProblem('${i.key}')">no log</button>
    </div>`;
  }).join("");
}

async function renderMembersGrid() {
  const d = await getJSON("/api/members");
  const box = $("membersGrid");
  box.innerHTML = (d.members || []).map((m) => `
    <button class="member-chip" onclick="openMemberProfile('${encodeURIComponent(m.username)}')">
      ${escapeHtml(m.username)}<span class="h">${escapeHtml(m.cf_handle || "")}</span>
    </button>`).join("");
}

async function openMemberProfile(encUser) {
  const user = decodeURIComponent(encUser);
  const card = $("memberProfileCard");
  $("memberProfileName").textContent = user + " — logs";
  card.classList.remove("hidden");
  $("memberLogList").innerHTML = `<p class="hint">loading…</p>`;
  const d = await getJSON("/api/logs?user=" + encodeURIComponent(user));
  $("memberLogList").innerHTML = renderLogItems(d.logs || [], d.problems || []);
  card.scrollIntoView({ behavior: "smooth", block: "nearest" });
}
window.openMemberProfile = openMemberProfile;

function renderLogItems(logs, problems) {
  if (!logs.length) return `<p class="hint">No logs shared yet.</p>`;
  const pmap = {}; (problems || []).forEach((p) => (pmap[p.key] = p));
  return logs.map((l) => {
    const p = pmap[l.problem_key];
    const link = p
      ? `<a class="log-prob" href="${p.url}" target="_blank" rel="noopener">${p.index} — ${escapeHtml(p.name)}</a>`
      : `<span class="log-prob">${escapeHtml(l.problem_key)}</span>`;
    return `<div class="log"><div class="log-head"><b>${escapeHtml(l.username)}</b>
      <span class="tag">${escapeHtml(l.tag)}</span>${link}<span>· ${fmtDate(l.timestamp)}</span></div>
      <div class="log-text">${escapeHtml(l.text)}</div></div>`;
  }).join("");
}

async function refreshStats() {
  const d = await getJSON("/api/mystats");
  if (d._status !== 200) return;
  renderBandsTable(d.bands);
  drawBars($("accChart"), d.bands.map((b) => b.rating),
           d.bands.map((b) => b.accuracy), 100, cssVar("--accent"), "%");
  drawBars($("timeChart"), d.timeline.map((t) => shortDate(t.date)),
           d.timeline.map((t) => t.solved),
           Math.max(1, ...d.timeline.map((t) => t.total)), cssVar("--success"), "");
  renderBookmarks();
}

function renderBandsTable(bands) {
  const t = $("bandsTable");
  if (!bands.length) { t.innerHTML = `<tbody><tr><td class="cell-none">No data yet — play a contest first.</td></tr></tbody>`; return; }
  let h = "<thead><tr><th>Rating</th><th>Attempted</th><th>Solved</th><th>Accuracy</th>" +
          "<th title='average minutes into the contest'>Avg elapsed</th>" +
          "<th title='average minutes actually spent on the problem'>Avg effort</th></tr></thead><tbody>";
  bands.forEach((b) => {
    h += `<tr><td>${b.rating}</td><td>${b.attempted}</td><td>${b.solved}</td>
      <td>${b.accuracy.toFixed(0)}%</td>
      <td>${b.avg_time_min ? b.avg_time_min.toFixed(0) + "′" : "–"}</td>
      <td>${b.avg_effort_min ? b.avg_effort_min.toFixed(0) + "′" : "–"}</td></tr>`;
  });
  t.innerHTML = h + "</tbody>";
}

async function renderBookmarks() {
  const d = await getJSON("/api/bookmarks");
  const box = $("bookmarkList");
  const items = d.items || [];
  BOOKMARKS = new Set(items.map((i) => i.key));
  if (!items.length) { box.innerHTML = `<p class="hint">No bookmarks yet.</p>`; return; }
  box.innerHTML = items.map((i) => `
    <div class="up-item">
      <a href="${i.url}" target="_blank" rel="noopener">${i.index}${i.name ? " — " + escapeHtml(i.name) : ""}</a>
      <button class="star on" title="remove bookmark" onclick="toggleBookmark('${i.key}')">★</button>
    </div>`).join("");
}

function drawBars(canvas, labels, values, maxVal, color, suffix) {
  const ctx = canvas.getContext("2d");
  const W = canvas.width, H = canvas.height;

  const cMuted = cssVar("--text-secondary"), cText = cssVar("--text-primary"),
        cBorder = cssVar("--border");
  ctx.clearRect(0, 0, W, H);
  if (!labels.length) {
    ctx.fillStyle = cMuted; ctx.font = "13px system-ui";
    ctx.fillText("No data yet.", 12, 24); return;
  }
  const padL = 34, padB = 26, padT = 12, padR = 12;
  const plotW = W - padL - padR, plotH = H - padT - padB;
  const n = labels.length, gap = 10;
  const bw = Math.max(6, (plotW - gap * (n - 1)) / n);
  ctx.strokeStyle = cBorder; ctx.fillStyle = cMuted; ctx.font = "11px system-ui";

  ctx.beginPath(); ctx.moveTo(padL, padT + plotH); ctx.lineTo(padL + plotW, padT + plotH); ctx.stroke();
  values.forEach((v, i) => {
    const x = padL + i * (bw + gap);
    const h = maxVal > 0 ? (v / maxVal) * plotH : 0;
    const y = padT + plotH - h;
    ctx.fillStyle = color; ctx.fillRect(x, y, bw, h);
    ctx.fillStyle = cText; ctx.textAlign = "center";
    ctx.fillText(Math.round(v) + suffix, x + bw / 2, y - 4 < padT ? padT + 10 : y - 4);
    ctx.fillStyle = cMuted;
    ctx.fillText(String(labels[i]), x + bw / 2, padT + plotH + 14);
  });
  ctx.textAlign = "left";
}

async function loadRules() {
  try {
    const html = await fetch("rules.html").then((r) => r.text());
    $("rulesCard").innerHTML = html;
  } catch (_) {
    $("rulesCard").innerHTML = `<p class="msg bad">Could not load rules.html</p>`;
  }
}

function fmtDate(ts) { return new Date(ts * 1000).toLocaleString(); }
function shortDate(ts) { const d = new Date(ts * 1000); return (d.getMonth() + 1) + "/" + d.getDate(); }

function fmtShort(ts) {
  if (!ts) return "—";
  const d = new Date(ts * 1000);
  const day = d.toLocaleDateString(undefined, { month: "short", day: "numeric" });
  const time = d.toLocaleTimeString(undefined, { hour: "numeric", minute: "2-digit" });
  return day + " · " + time;
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
}

document.querySelectorAll(".tab").forEach((t) => (t.onclick = () => setView(t.dataset.view)));

$("themeToggle").onclick = toggleTheme;
applyThemeIcon();

$("loginBtn").onclick = async () => {
  const msg = $("loginMsg"); msg.className = "msg";
  const r = await postJSON("/api/login", {
    username: $("li_user").value.trim(), password: $("li_pass").value });
  if (r.ok) { $("li_pass").value = ""; await refreshWhoAmI(); }
  else { msg.className = "msg bad"; msg.textContent = r.error || "login failed"; }
};

$("logoutBtn").onclick = async () => { await postJSON("/api/logout", {}); ME = null; renderAuth(); };

$("createBtn").onclick = async () => {
  const msg = $("createMsg"); msg.className = "msg";
  const ratings = $("cc_ratings").value.split(",").map((x) => parseInt(x.trim(), 10))
    .filter((x) => !isNaN(x));
  const duration = parseInt($("cc_duration").value, 10);
  if (ratings.length === 0) { msg.className = "msg bad"; msg.textContent = "enter at least one rating"; return; }

  let scheduled_at = 0;
  const schedRaw = $("cc_schedule").value;
  if (schedRaw) {
    const t = Math.floor(new Date(schedRaw).getTime() / 1000);
    if (t > 0) scheduled_at = t;
  }
  msg.textContent = "selecting problems… (checking everyone's solved list, ~a few seconds)";
  const r = await postJSON("/api/admin/create_contest", { ratings, duration_min: duration, scheduled_at });
  if (r.ok) {
    msg.className = "msg ok"; msg.textContent = `${r.name}: ${r.count} problems selected (hidden).`;
    $("draftBox").classList.remove("hidden");
    $("draftInfo").textContent = scheduled_at
      ? `${r.name} scheduled — it opens automatically at the set time. You can also start it now.`
      : `${r.name} ready with ${r.count} problems. Start when you're ready.`;
  } else { msg.className = "msg bad"; msg.textContent = r.error || "could not create contest"; }
};

$("startBtn").onclick = async () => {
  const msg = $("startMsg"); msg.className = "msg";
  const r = await postJSON("/api/admin/start", {});
  if (r.ok) {
    msg.className = "msg ok"; msg.textContent = "Started! Switch to the Contest tab.";
    $("draftBox").classList.add("hidden");
  } else { msg.className = "msg bad"; msg.textContent = r.error || "could not start"; }
};

$("registerBtn").onclick = async () => {
  const msg = $("registerMsg"); msg.className = "msg";
  const r = await postJSON("/api/admin/register", {
    username: $("rg_user").value.trim(), password: $("rg_pass").value,
    cf_handle: $("rg_handle").value.trim(), role: $("rg_role").value });
  if (r.ok) {
    msg.className = "msg ok"; msg.textContent = `Created "${r.username}" (${r.role}). Email them their login.`;
    $("rg_user").value = ""; $("rg_pass").value = ""; $("rg_handle").value = "";
  } else { msg.className = "msg bad"; msg.textContent = r.error || "could not create account"; }
};

$("refreshBtn").onclick = async () => {
  const msg = $("refreshMsg"); msg.className = "msg";
  msg.textContent = "refreshing… (a few seconds)";
  const r = await postJSON("/api/admin/refresh_cache", {});
  if (r.ok) { msg.className = "msg ok"; msg.textContent = `Cached ${r.count} problems.`; }
  else { msg.className = "msg bad"; msg.textContent = r.error || "refresh failed"; }
};

$("modalClose").onclick = closeModal;
$("modal").onclick = (e) => { if (e.target === $("modal")) closeModal(); };
document.addEventListener("keydown", (e) => { if (e.key === "Escape") closeModal(); });
$("closeProfileBtn").onclick = () => $("memberProfileCard").classList.add("hidden");

pollTimer = setInterval(() => {
  if (!ME || CURRENT_VIEW !== "contest") return;
  if (CONTEST_MODE === "detail") refreshState();
  else refreshArchive();
}, 15000);
$("backToList").onclick = showContestList;
tickTimer = setInterval(updateTimer, 1000);

refreshWhoAmI();
