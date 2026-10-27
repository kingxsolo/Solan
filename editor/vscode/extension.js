const vscode = require("vscode");
const { LanguageClient } = require("vscode-languageclient/node");
let client;
exports.activate = function (ctx) {
  const cmd = vscode.workspace.getConfiguration("solan").get("path") || "solan";
  client = new LanguageClient("solan", "Solan", { command: cmd, args: ["lsp"] },
    { documentSelector: [{ scheme: "file", language: "solan" }] });
  client.start();
};
exports.deactivate = function () { return client ? client.stop() : undefined; };
