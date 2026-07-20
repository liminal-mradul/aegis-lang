// Aegis Language Support — VS Code extension
//
// Cross-platform by construction: VS Code itself runs identically on
// Windows/Linux/macOS, and this extension only shells out to the
// `aegis` executable (which the user builds/installs separately on
// each OS) via Node's child_process — no OS-specific code here at all.
"use strict";

const vscode = require("vscode");
const cp = require("child_process");
const path = require("path");

const DIAG_RE = /\[(LexError|ParseError|SemError|Warning)\]\s+(.*?)\s+at line\s+(\d+),\s*col\s+(\d+)/g;
const ANSI_RE = /\x1b\[[0-9;]*m/g;

let diagnosticCollection;
let outputChannel;
let debounceTimer;

function getConfig() {
  const cfg = vscode.workspace.getConfiguration("aegis");
  return {
    exePath: cfg.get("executablePath", "aegis"),
    checkOnSave: cfg.get("checkOnSave", true),
    checkOnType: cfg.get("checkOnType", false),
  };
}

/**
 * Parse the compiler's own diagnostic format:
 *   [SemError] message at line L, col C
 *   [Warning]  message at line L, col C
 *   [ParseError] message at line L, col C
 *   [LexError] message at line L, col C
 * This is the exact format `to_string()` produces in lexer.hpp/parser.hpp/
 * sema.hpp — parsed here rather than guessed, so this stays in sync with
 * how the compiler actually talks.
 */
function parseDiagnostics(rawOutput, document) {
  const text = rawOutput.replace(ANSI_RE, "");
  const diagnostics = [];
  let match;
  DIAG_RE.lastIndex = 0;
  while ((match = DIAG_RE.exec(text)) !== null) {
    const [, kind, message, lineStr, colStr] = match;
    const line = Math.max(0, parseInt(lineStr, 10) - 1);
    const col = Math.max(0, parseInt(colStr, 10) - 1);
    const range = new vscode.Range(
      new vscode.Position(line, col),
      new vscode.Position(line, col + 1)
    );
    const severity =
      kind === "Warning"
        ? vscode.DiagnosticSeverity.Warning
        : vscode.DiagnosticSeverity.Error;
    const diag = new vscode.Diagnostic(range, message, severity);
    diag.source = "aegis";
    diag.code = kind;
    diagnostics.push(diag);
  }
  return diagnostics;
}

function runCheck(document) {
  if (document.languageId !== "aegis") return;
  if (document.isUntitled) return;

  const { exePath } = getConfig();
  const filePath = document.fileName;
  const cwd = path.dirname(filePath);

  cp.execFile(
    exePath,
    ["check", filePath],
    { cwd, timeout: 10000 },
    (_err, stdout, stderr) => {
      // A non-zero exit code from `aegis check` is expected (it means
      // "found errors") — that's not a failure of the check itself.
      // We only bail out early if the executable couldn't be run at all.
      if (_err && _err.code === undefined && _err.errno) {
        outputChannel.appendLine(
          `[aegis] Could not run '${exePath}'. Is it installed and on PATH, ` +
            `or set "aegis.executablePath" in settings? (${_err.message})`
        );
        diagnosticCollection.delete(document.uri);
        return;
      }
      const combined = (stdout || "") + "\n" + (stderr || "");
      const diags = parseDiagnostics(combined, document);
      diagnosticCollection.set(document.uri, diags);
    }
  );
}

function scheduleCheck(document, delayMs) {
  clearTimeout(debounceTimer);
  debounceTimer = setTimeout(() => runCheck(document), delayMs);
}

function activate(context) {
  diagnosticCollection = vscode.languages.createDiagnosticCollection("aegis");
  outputChannel = vscode.window.createOutputChannel("Aegis");
  context.subscriptions.push(diagnosticCollection, outputChannel);

  // Check immediately for any .ae files already open when the extension starts
  vscode.workspace.textDocuments.forEach((doc) => {
    if (doc.languageId === "aegis") runCheck(doc);
  });

  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((doc) => {
      if (doc.languageId === "aegis") runCheck(doc);
    })
  );

  context.subscriptions.push(
    vscode.workspace.onDidSaveTextDocument((doc) => {
      const { checkOnSave } = getConfig();
      if (doc.languageId === "aegis" && checkOnSave) runCheck(doc);
    })
  );

  context.subscriptions.push(
    vscode.workspace.onDidChangeTextDocument((e) => {
      const { checkOnType } = getConfig();
      if (e.document.languageId === "aegis" && checkOnType) {
        scheduleCheck(e.document, 500);
      }
    })
  );

  context.subscriptions.push(
    vscode.workspace.onDidCloseTextDocument((doc) => {
      diagnosticCollection.delete(doc.uri);
    })
  );

  // Manual command: "Aegis: Check Current File"
  context.subscriptions.push(
    vscode.commands.registerCommand("aegis.checkFile", () => {
      const editor = vscode.window.activeTextEditor;
      if (editor && editor.document.languageId === "aegis") {
        runCheck(editor.document);
      } else {
        vscode.window.showInformationMessage("Open an .ae file first.");
      }
    })
  );
}

function deactivate() {
  clearTimeout(debounceTimer);
}

module.exports = { activate, deactivate };
