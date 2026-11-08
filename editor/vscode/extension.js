// إضافة سولان لـ VS Code — عميل خادم لغة مصغّر بلا تبعيات خارجية
// يتصل بـ `solan lsp` عبر stdin/stdout بإطار JSON-RPC القياسي
// + مزوّد إكمال ذكي: يقترح الدوال والكلمات مرتبة حسب الأكثر استخداماً
//   (استخدامك الفعلي المحفوظ + تكرار الكلمة في الملف + شعبية مبدئية)
"use strict";
const vscode = require("vscode");
const { spawn } = require("child_process");
const data = require("./data");

// ==================== عميل خادم اللغة (تشخيص) ====================

let proc = null;
let outChan = null;
let seq = 0;
let started = false;
let restarting = false;
const pending = new Map(); // id -> {resolve, reject}
const diagnostics = vscode.languages.createDiagnosticCollection("solan");
let outBuf = Buffer.alloc(0);

function serverCmd() {
  return vscode.workspace.getConfiguration("solan").get("path") || "solan";
}

// تمرير رسالة خام للخادم (إشعارات بلا رقم)
function notify(method, params) {
  sendFrame({ jsonrpc: "2.0", method, params });
}

// طلب ينتظر رداً (with id)
function request(method, params) {
  return new Promise((resolve, reject) => {
    const id = ++seq;
    pending.set(id, { resolve, reject });
    sendFrame({ jsonrpc: "2.0", id, method, params });
  });
}

function sendFrame(obj) {
  if (!proc || !proc.stdin.writable) return;
  const body = Buffer.from(JSON.stringify(obj), "utf8");
  const head = Buffer.from(
    "Content-Length: " + body.length + "\r\n\r\n", "ascii");
  try { proc.stdin.write(head); proc.stdin.write(body); }
  catch (e) { /* الخادم مات؛ سيعاد التشغيل عند النشاط التالي */ }
}

// قراءة الرسائل من دفق stdout الخام (تجميع + شطرت الإطار)
function handleData(chunk) {
  outBuf = Buffer.concat([outBuf, chunk]);
  for (;;) {
    // بحث عن نهاية الترويسة
    const sep = outBuf.indexOf("\r\n\r\n");
    if (sep < 0) return;
    const head = outBuf.slice(0, sep).toString("ascii");
    const m = /Content-Length:\s*(\d+)/i.exec(head);
    if (!m) { outBuf = Buffer.alloc(0); return; }
    const len = parseInt(m[1], 10);
    if (outBuf.length < sep + 4 + len) return;
    const body = outBuf.slice(sep + 4, sep + 4 + len);
    outBuf = outBuf.slice(sep + 4 + len);
    let msg;
    try { msg = JSON.parse(body.toString("utf8")); }
    catch (e) { continue; }
    dispatch(msg);
  }
}

function dispatch(msg) {
  if (msg && typeof msg.id !== "undefined" && pending.has(msg.id)) {
    const p = pending.get(msg.id);
    pending.delete(msg.id);
    if (msg.error) p.reject(new Error(msg.error.message || "خطأ خادم"));
    else p.resolve(msg.result);
    return;
  }
  if (msg.method === "textDocument/publishDiagnostics") return onDiag(msg.params);
  if (outChan && msg.method) outChan.appendLine("استقبل: " + msg.method);
  // رسائل أخرى تُسجل فقط
}

function onDiag(p) {
  if (!p || !p.uri) return;
  const uri = vscode.Uri.parse(p.uri);
  const diags = (p.diagnostics || []).map(d => new vscode.Diagnostic(
    new vscode.Range(
      Math.max(0, (d.range.start.line || 0)),
      Math.max(0, (d.range.start.character || 0)),
      Math.max(0, (d.range.end.line || 0)),
      Math.max(0, (d.range.end.character || 0))),
    d.message || "", d.severity || 0));
  diagnostics.set(uri, diags);
}

function start() {
  if (started) return;
  started = true;
  if (!outChan) outChan = vscode.window.createOutputChannel("سولان LSP");
  const cmd = serverCmd();
  outChan.appendLine("تشغيل: " + cmd + " lsp");
  try {
    proc = spawn(cmd, ["lsp"], { stdio: ["pipe", "pipe", "pipe"] });
  } catch (e) {
    outChan.appendLine("فشل تشغيل الخادم: " + e.message);
    started = false;
    return;
  }
  proc.stdout.on("data", handleData);
  proc.stderr.on("data", d => outChan.appendLine("خادم: " + d.toString("utf8")));
  proc.on("exit", code => {
    outChan.appendLine("أُغلق الخادم بكود " + code);
    proc = null;
    started = false;
  });
  proc.on("error", e => {
    outChan.appendLine("خطأ في الخادم: " + e.message);
    outChan.appendLine("تأكد من تثبيت سولان ('" + serverCmd() + "') أو اضبط الإعداد solan.path.");
    proc = null;
    started = false;
  });

  request("initialize", {
    processId: process.pid,
    rootUri: (vscode.workspace.workspaceFolders && vscode.workspace.workspaceFolders[0])
      ? vscode.workspace.workspaceFolders[0].uri.toString() : null,
    capabilities: {
      textDocument: {
        publishDiagnostics: { relatedInformation: false }
      }
    }
  }).then(() => notify("initialized", {}));
}

function stop() {
  if (proc) {
    try { notify("shutdown", {}); notify("exit", {}); proc.kill(); } catch (e) {}
    proc = null;
  }
  started = false;
}

// ربط تفعيل إغلاق/تفتح الملفات والتعديلات بمزامنة الخادم
const listeners = [];

function onOpen(doc) {
  if (!isSolan(doc)) return;
  start();
  const uri = doc.uri;
  notify("textDocument/didOpen", {
    textDocument: {
      uri: uri.toString(),
      languageId: "solan",
      version: doc.version,
      text: doc.getText()
    }
  });
}

function onDocsChanged(e) {
  const doc = e.document;
  if (!isSolan(doc)) return;
  // إرسال النص الكامل عند أي تعديل (أبسط وأضمن توافق مع الخادم البسيط)
  notify("textDocument/didChange", {
    textDocument: { uri: doc.uri.toString(), version: doc.version },
    contentChanges: [{ text: doc.getText() }]
  });
}

function onClose(doc) {
  if (!isSolan(doc)) return;
  notify("textDocument/didClose", {
    textDocument: { uri: doc.uri.toString() }
  });
  diagnostics.delete(doc.uri);
}

function restart() {
  restarting = true;
  stop();
  restarting = false;
}

// ==================== الإكمال الذكي ====================

// محدد الوثائق: اللغة + أنماط الامتدادات (يضمن العمل في .سو و.an و.solan
// حتى لو ربطت إضافة أخرى الامتداد بلغة مختلفة)
const SELECTOR = [
  { language: "solan" },
  { pattern: "**/*.سو" },
  { pattern: "**/*.an" },
  { pattern: "**/*.solan" }
];

// هل الوثيقة ملف سولان؟ (باللغة أو بالامتداد)
function isSolan(doc) {
  if (doc.languageId === "solan") return true;
  try { return /\.(سو|an|solan)$/i.test(doc.uri.fsPath || ""); }
  catch (e) { return false; }
}

// عدد مرات قبولك لكل اقتراح — محفوظ عبر الجلسات (globalState)
let usage = {};
let globalCtx = null;

function bumpUsage(name) {
  if (!name) return;
  usage[name] = (usage[name] || 0) + 1;
  if (globalCtx) globalCtx.globalState.update("solan.usage", usage);
}

// تكرار كل كلمة في الملف الحالي (ذاكرة مؤقتة بحسب نسخة المستند)
let freqDoc = null, freqVer = -1, freqMap = null;
function docFreqs(doc) {
  if (freqDoc === doc && freqVer === doc.version && freqMap) return freqMap;
  const text = doc.getText();
  const map = new Map();
  const re = /[\u0600-\u06FF\w_]+/g;
  let m;
  while ((m = re.exec(text)) !== null) {
    const w = m[0];
    map.set(w, (map.get(w) || 0) + 1);
  }
  freqDoc = doc; freqVer = doc.version; freqMap = map;
  return map;
}

// ---------- جمع التعريفات من نص (متغيرات، دوال، معاملات، ثوابت، أصناف) ----------
function collectDefs(text, topLevelOnly) {
  const out = [];
  const seen = new Set();
  const add = (n, k, d) => {
    n = (n || "").trim();
    if (!n || seen.has(n)) return;
    if (!/^[\u0600-\u06FF\w_]+$/.test(n)) return;
    seen.add(n);
    out.push({ n, k, d });
  };
  const scan = (re, fn) => {
    re.lastIndex = 0;
    let m;
    while ((m = re.exec(text)) !== null) {
      // m[2] = الإزاحة البادئة — في المسح الشامل نقبل المستوى الأعلى فقط
      if (topLevelOnly && m[2] && m[2].length) continue;
      fn(m);
    }
  };
  const K = vscode.CompletionItemKind;
  // دالة مع معاملاتها
  scan(/(^|\n)([ \t]*)(?:غير_متزامن[ \t]+|ساكن[ \t]+|مولد[ \t]+)?دالة[ \t]+([^\s({]+)[ \t]*\(([^)]*)\)/g,
    m => {
      add(m[3], K.Function, "دالة");
      for (let p of m[4].split(/[،,]/)) {
        p = p.trim().split(/[:\s]/)[0];
        add(p, K.Variable, "معامل دالة");
      }
    });
  // متغيرات وثوابت
  scan(/(^|\n)([ \t]*)خل[ \t]+([^\s=:，(]+)/g, m => add(m[3], K.Variable, "متغير"));
  scan(/(^|\n)([ \t]*)ثابت[ \t]+([^\s=:]+)/g, m => add(m[3], K.Constant, "ثابت"));
  // أصناف
  scan(/(^|\n)([ \t]*)صنف[ \t]+([^\s{]+)/g, m => add(m[3], K.Class, "صنف"));
  // متغيرات حلقة لكل
  scan(/(^|\n)([ \t]*)لكل[ \t]+([^\s]+)[ \t]+في/gm, m => add(m[3], K.Variable, "متغير حلقة"));
  // متغيرات امسك (الخطأ)
  scan(/(^|\n)([ \t]*)\}[ \t]*امسك[ \t]*\(\s*([^)\s]+)\s*\)/g, m => add(m[3], K.Variable, "متغير الخطأ"));
  return out;
}

// ---------- تعريفات المشروع كله (مسح كسول مخزن) ----------
let wsDefs = [];        // [{n, k, file}]
let wsScanned = false;
let wsScanTimer = null;

function scheduleWsScan() {
  if (wsScanTimer) return;
  wsScanTimer = setTimeout(() => { wsScanTimer = null; scanWorkspace(); }, 800);
}

async function scanWorkspace() {
  try {
    const folders = vscode.workspace.workspaceFolders;
    if (!folders || !folders.length) { wsDefs = []; wsScanned = true; return; }
    const found = new Map();
    for (const p of ["**/*.سو", "**/*.an", "**/*.solan"]) {
      try {
        for (const u of await vscode.workspace.findFiles(p, "**/{node_modules,.git,dist,release}/**", 200))
          found.set(u.toString(), u);
      } catch (e) {}
    }
    const defs = new Map();
    for (const u of found.values()) {
      try {
        const bytes = await vscode.workspace.fs.readFile(u);
        const text = Buffer.from(bytes).toString("utf8");
        for (const d of collectDefs(text, true)) {
          if (!defs.has(d.n)) defs.set(d.n, { n: d.n, k: d.k, file: u.path.split("/").pop() });
        }
      } catch (e) { /* ملف غير قابل للقراءة — تجاوز */ }
    }
    wsDefs = [...defs.values()];
    wsScanned = true;
  } catch (e) { wsScanned = true; }
}

// سياق الإكمال عند المؤشر: نطاق الكلمة، هل نحن بعد نقطة، واسم الكائن قبلها
function completionContext(doc, pos) {
  const line = doc.lineAt(pos.line).text;
  const isWord = ch => /[\u0600-\u06FF\w_]/.test(ch);
  const end = Math.min(pos.character, line.length);
  let b = end;
  while (b < line.length && isWord(line[b])) b++;
  let a = end;
  while (a > 0 && isWord(line[a - 1])) a--;
  const word = line.slice(a, end);
  // هل يسبق الكلمة نقطة (مع تجاهل الفراغات)؟ ومن هو قبل النقطة؟
  let i = a - 1;
  while (i >= 0 && line[i] === " ") i--;
  const afterDot = i >= 0 && line[i] === ".";
  let owner = "";
  if (afterDot) {
    let e2 = i;
    let s2 = i;
    while (s2 > 0 && isWord(line[s2 - 1])) s2--;
    owner = line.slice(s2, e2);
  }
  return {
    range: word ? new vscode.Range(pos.line, a, pos.line, end) : null,
    prefix: word,
    afterDot,
    owner
  };
}

// هل الحرف التالي للكلمة قوس فتح؟ (نتجنب إدخال قوس مكرر)
function nextIsOpenParen(doc, ctx) {
  const line = doc.lineAt(ctx.range ? ctx.range.end.line : 0).text;
  let i = ctx.range ? ctx.range.end.character : Math.min(ctx.cursor, line.length);
  while (i < line.length && line[i] === " ") i++;
  return line[i] === "(";
}

function md(s) { return new vscode.MarkdownString(s); }

// عنصر إكمال جاهز مع الترتيب والتوثيق
function makeItem(o, ctx, freqs) {
  // o: {label, kind, detail, doc, insert, pop, filter, isFn}
  const item = new vscode.CompletionItem(o.label, o.kind);
  item.detail = o.detail || "";
  if (o.doc) item.documentation = md(o.doc);
  item.insertText = o.insert;
  if (o.isFn) item.insertTextRules = vscode.CompletionItemInsertTextRule.InsertAsSnippet;
  if (o.filter && o.filter !== o.label) item.filterText = o.filter;
  if (ctx.range) item.range = ctx.range;
  // الترتيب: الأكثر استخداماً أولاً (score أكبر = sortText أصغر)
  const score = (o.pop || 0) + (usage[o.label] || 0) * 12 + (freqs.get(o.label) || 0) * 6;
  item.sortText = String(999999 - score).padStart(6, "0");
  // تسجيل الاستخدام عند القبول ليتحسن الترتيب مع الوقت
  item.command = { command: "solan.completionAccepted", title: "تسجيل الاستخدام", arguments: [o.label] };
  return item;
}

// أدخل الاسم كاملاً: مع القوسين ومؤشر داخله، وباقتباسات جاهزة
// للدوال التي تبدأ بمعامل نصي (اطبع("") والمؤشر بين علامتي التنصيص)
// وإلا الاسم وحده إذا كتب المستخدم القوس قبله (دوال داخل دوال)
function fnInsert(name, doc, ctx, wantsQuote) {
  if (nextIsOpenParen(doc, ctx)) return name;
  return wantsQuote ? name + "(\"$0\")" : name + "($0)";
}

function provideItems(doc, pos) {
  const ctx = completionContext(doc, pos);
  ctx.cursor = pos.character;
  const freqs = docFreqs(doc);
  const items = [];
  const F = n => freqs.get(n) || 0;

  // ---- بعد نقطة: طرق القيم ----
  if (ctx.afterDot) {
    for (const m of data.METHODS) {
      const isFn = !m.prop;
      items.push(makeItem({
        label: m.n,
        kind: isFn ? vscode.CompletionItemKind.Method : vscode.CompletionItemKind.Property,
        detail: m.prop ? "خاصية مدمجة" : "طريقة مدمجة — " + (m.s || m.n + "()"),
        doc: m.d ? "**" + (m.s || m.n) + "**\n\n" + m.d : undefined,
        insert: isFn ? fnInsert(m.n, doc, ctx, m.q) : m.n,
        isFn: isFn && !nextIsOpenParen(doc, ctx),
        pop: m.p || 20
      }, ctx, freqs));
    }
    return items;
  }

  // ---- تعريفات المستخدم: هذا الملف (كل المستويات) ثم بقية المشروع ----
  const text = doc.getText();
  const defs = collectDefs(text, false);
  const seen = new Set();
  for (const def of defs) {
    seen.add(def.n);
    const isFnDef = def.k === vscode.CompletionItemKind.Function;
    items.push(makeItem({
      label: def.n, kind: def.k,
      detail: def.d + " — في هذا الملف",
      insert: isFnDef ? fnInsert(def.n, doc, ctx) : def.n,
      isFn: isFnDef && !nextIsOpenParen(doc, ctx),
      pop: 120 // تعريفات المستخدم أولاً دائماً
    }, ctx, freqs));
  }
  // تعريفات المستوى الأعلى من بقية ملفات سولان في المشروع
  if (!wsScanned) scheduleWsScan();
  for (const d of wsDefs) {
    if (seen.has(d.n)) continue;
    seen.add(d.n);
    const isFnDef = d.k === vscode.CompletionItemKind.Function;
    items.push(makeItem({
      label: d.n, kind: d.k,
      detail: d.d + " — معرف في " + d.file,
      insert: isFnDef ? fnInsert(d.n, doc, ctx) : d.n,
      isFn: isFnDef && !nextIsOpenParen(doc, ctx),
      pop: 70
    }, ctx, freqs));
  }

  // ---- الدوال المدمجة والوحدات العامة ----
  for (const f of data.BUILTINS) {
    const isMod = !f.s; // مدخلات الوحدات (نظام، جيسون...) بلا توقيع
    items.push(makeItem({
      label: f.n,
      kind: isMod ? vscode.CompletionItemKind.Module : vscode.CompletionItemKind.Function,
      detail: isMod ? "وحدة أصلية" : "دالة مدمجة — " + f.s,
      doc: f.d ? "**" + (f.s || f.n) + "**\n\n" + f.d : undefined,
      insert: isMod ? f.n : fnInsert(f.n, doc, ctx, f.q),
      isFn: !isMod && !nextIsOpenParen(doc, ctx),
      pop: f.p || 15,
      filter: data.norm(f.n) !== f.n ? f.n + " " + data.norm(f.n) : undefined
    }, ctx, freqs));
  }

  // ---- الوحدات (أصلية + قياسية) ----
  for (const mod of [...data.NATIVE_MODULES, ...data.STDLIB_MODULES]) {
    items.push(makeItem({
      label: mod, kind: vscode.CompletionItemKind.Module,
      detail: "مكتبة — استورد \"مكتبة\"",
      insert: mod, pop: 25,
      filter: data.norm(mod) !== mod ? mod + " " + data.norm(mod) : undefined
    }, ctx, freqs));
  }

  // ---- العمليات = + == وغيرها: فرادى وبلا أي إضافات ليختار منها ----
  for (const op of data.OPERATORS) {
    items.push(makeItem({
      label: op.n, kind: vscode.CompletionItemKind.Operator,
      detail: op.d, insert: op.n, pop: op.p || 12
    }, ctx, freqs));
  }

  // ---- الكلمات المفتاحية القياسية ثم البدائل ----
  for (const k of data.KEYWORDS) {
    items.push(makeItem({
      label: k, kind: vscode.CompletionItemKind.Keyword,
      detail: "كلمة مفتاحية", insert: k, pop: 55,
      filter: data.norm(k) !== k ? k + " " + data.norm(k) : undefined
    }, ctx, freqs));
  }
  for (const a of data.ALIASES) {
    items.push(makeItem({
      label: a, kind: vscode.CompletionItemKind.Keyword,
      detail: "كلمة مفتاحية (صيغة بديلة)", insert: a, pop: 12,
      filter: data.norm(a) !== a ? a + " " + data.norm(a) : undefined
    }, ctx, freqs));
  }

  return items;
}

// حروف العمليات — كتابتها تعرض قائمة العمليات وحدها
const OP_CHARS = "=+-*/%<>!&|";

const completionProvider = {
  async provideCompletionItems(doc, pos, token, context) {
    // كتبت عملية (= + < …)؟ اعرض العمليات فرادى لاختيارها فوراً
    if (context && context.triggerKind === vscode.CompletionTriggerKind.TriggerCharacter &&
        context.triggerCharacter && OP_CHARS.includes(context.triggerCharacter)) {
      const ctx = completionContext(doc, pos);
      ctx.cursor = pos.character;
      const freqs = docFreqs(doc);
      return data.OPERATORS.map(op => makeItem({
        label: op.n, kind: vscode.CompletionItemKind.Operator,
        detail: op.d, insert: op.n, pop: op.p || 12
      }, ctx, freqs));
    }
    const local = provideItems(doc, pos);
    // بعد النقطة: اسأل خادم اللغة عن أعضاء الوحدة (نظام.، ملفات.…) إن كان يعمل
    const ctx = completionContext(doc, pos);
    if (ctx.afterDot && proc) {
      try {
        const res = await Promise.race([
          request("textDocument/completion", {
            textDocument: { uri: doc.uri.toString() },
            position: { line: pos.line, character: pos.character }
          }),
          new Promise((_, rej) => setTimeout(() => rej(new Error("مهلة")), 400))
        ]);
        const raw = (res && res.items) || res || [];
        const have = new Set(local.map(i => i.label));
        const map = { 2: 1, 3: 2, 5: 6, 6: 5, 7: 6, 9: 8, 12: 2, 13: 5, 14: 13 };
        for (const it of raw) {
          if (!it || !it.label || have.has(it.label)) continue;
          have.add(it.label);
          const ci = new vscode.CompletionItem(it.label, map[it.kind] || vscode.CompletionItemKind.Function);
          ci.detail = it.detail || "عضو في مكتبة " + ctx.owner;
          ci.insertText = it.label;
          if (ctx.range) ci.range = ctx.range;
          ci.sortText = String(999999 - (60 + (usage[it.label] || 0) * 12)).padStart(6, "0");
          ci.command = { command: "solan.completionAccepted", title: "تسجيل الاستخدام", arguments: [it.label] };
          local.push(ci);
        }
      } catch (e) { /* الخادم غير متاح — يكفي الإكمال المحلي */ }
    }
    return local;
  }
};

// حروف التفعيل: العربية واللاتينية والنقطة وحروف العمليات (اقتراحات أثناء الكتابة)
const TRIGGERS = "ابتثجحخدذرزسشصضطظعغفقكلمنهويةءآأإئؤةى_".split("")
  .concat("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ".split(""))
  .concat([".", "=", "+", "-", "*", "/", "%", "<", ">", "!", "&", "|"]);

// ==================== التحويم (hover) ====================

const hoverProvider = {
  provideHover(doc, pos) {
    const ctx = completionContext(doc, pos);
    if (!ctx.range) return null;
    const word = ctx.prefix;
    const n = data.norm(word);
    const f = data.BUILTINS.find(x => x.n === word || data.norm(x.n) === n);
    if (f) {
      const parts = ["**" + (f.s || f.n) + "**"];
      if (f.d) parts.push(f.d);
      parts.push("_" + (f.s ? "دالة مدمجة في سولان" : "وحدة أصلية في سولان") + "_");
      return new vscode.Hover(md(parts.join("\n\n")), ctx.range);
    }
    const m = data.METHODS.find(x => x.n === word || data.norm(x.n) === n);
    if (m) {
      const parts = ["**" + (m.s || m.n) + "**"];
      if (m.d) parts.push(m.d);
      parts.push("_" + (m.prop ? "خاصية مدمجة للقيم" : "طريقة مدمجة للقيم") + "_");
      return new vscode.Hover(md(parts.join("\n\n")), ctx.range);
    }
    if (data.KEYWORDS.includes(word) || data.ALIASES.includes(word)) {
      return new vscode.Hover(md("**" + word + "**\n\nكلمة مفتاحية في سولان"), ctx.range);
    }
    return null;
  }
};

// ==================== التفعيل ====================

exports.activate = function (ctx) {
  globalCtx = ctx;
  usage = ctx.globalState.get("solan.usage", {}) || {};

  // أمر داخلي: يُستدعى عند قبول اقتراح ليزيد ترتيبه مستقبلاً
  ctx.subscriptions.push(vscode.commands.registerCommand("solan.completionAccepted", bumpUsage));

  ctx.subscriptions.push(
    vscode.languages.registerCompletionItemProvider(
      SELECTOR, completionProvider, ...TRIGGERS),
    vscode.languages.registerHoverProvider(SELECTOR, hoverProvider)
  );

  listeners.push(
    vscode.workspace.onDidOpenTextDocument(onOpen),
    vscode.workspace.onDidChangeTextDocument(onDocsChanged),
    vscode.workspace.onDidCloseTextDocument(onClose),
    vscode.workspace.onDidSaveTextDocument(() => scheduleWsScan()),
    vscode.workspace.onDidChangeWorkspaceFolders(() => { wsScanned = false; scheduleWsScan(); }),
    vscode.workspace.onDidChangeConfiguration(e => {
      if (e.affectsConfiguration("solan.path")) restart();
    })
  );
  ctx.subscriptions.push(diagnostics, ...listeners);
  if (outChan) ctx.subscriptions.push(outChan);
  // افتح أي ملفات سولان مفتوحة مسبقاً
  for (const doc of vscode.workspace.textDocuments) onOpen(doc);
  // امسح تعريفات المشروع عند التفعيل
  scanWorkspace();

  // سؤال واحد عن أيقونات سولان في المستكشف (سمتنا تعرضها حتماً)
  if (!ctx.globalState.get("solan.iconThemeAsked", false)) {
    ctx.globalState.update("solan.iconThemeAsked", true);
    const cur = vscode.workspace.getConfiguration("workbench").get("iconTheme");
    if (cur !== "solan-icons") {
      vscode.window.showInformationMessage(
        "هل تظهر أيقونة سولان على ملفات .سو في المستكشف؟",
        "نعم، استخدم أيقونات سولان", "لا، شكراً"
      ).then(pick => {
        if (pick === "نعم، استخدم أيقونات سولان") {
          vscode.workspace.getConfiguration("workbench").update(
            "workbench.iconTheme", "solan-icons",
            vscode.ConfigurationTarget.Global);
        }
      });
    }
  }
};

exports.deactivate = function () {
  stop();
};
