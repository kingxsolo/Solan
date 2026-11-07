// إضافة سولان لـ VS Code — عميل خادم لغة مصغّر بلا تبعيات خارجية
// يتصل بـ `solan lsp` عبر stdin/stdout بإطار JSON-RPC القياسي
"use strict";
const vscode = require("vscode");
const { spawn } = require("child_process");

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

function uriOf(doc) {
  return doc.isUntitled ? null : doc.uri;
}

function onOpen(doc) {
  if (doc.languageId !== "solan") return;
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
  if (doc.languageId !== "solan") return;
  // إرسال النص الكامل عند أي تعديل (أبسط وأضمن توافق مع الخادم البسيط)
  notify("textDocument/didChange", {
    textDocument: { uri: doc.uri.toString(), version: doc.version },
    contentChanges: [{ text: doc.getText() }]
  });
}

function onClose(doc) {
  if (doc.languageId !== "solan") return;
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

exports.activate = function (ctx) {
  listeners.push(
    vscode.workspace.onDidOpenTextDocument(onOpen),
    vscode.workspace.onDidChangeTextDocument(onDocsChanged),
    vscode.workspace.onDidCloseTextDocument(onClose),
    vscode.workspace.onDidChangeConfiguration(e => {
      if (e.affectsConfiguration("solan.path")) restart();
    })
  );
  ctx.subscriptions.push(diagnostics, ...listeners);
  if (outChan) ctx.subscriptions.push(outChan);
  // افتح أي ملفات سولان مفتوحة مسبقاً
  for (const doc of vscode.workspace.textDocuments) onOpen(doc);
};

exports.deactivate = function () {
  stop();
};
