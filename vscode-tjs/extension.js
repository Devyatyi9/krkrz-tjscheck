const vscode = require("vscode");
const { execFile } = require("child_process");
const path = require("path");
const fs = require("fs");

// tjscheck reports one error and stops, as "<stdin>:line:col: syntax error: text".
// Line and column count from one; the editor counts from zero.
const ERROR_LINE = /^(.+?):(\d+):(\d+):\s*(.+)$/;

let diagnostics;
let timers = new Map();

// Configured path first, then the checker built beside this extension, then a
// copy kept in the open project. The middle one is what makes the extension
// work on its own, outside any particular workspace.
function checkerPath() {
	const configured = vscode.workspace.getConfiguration("tjscheck").get("checkerPath");
	if (configured) {
		return configured;
	}
	// Beside the extension is where a shipped copy goes; one level up is the
	// build in this repository, for working from source. Nothing is guessed
	// from the open workspace: that would pick up whatever unrelated project
	// happens to carry a file by this name.
	const candidates = [
		path.join(__dirname, "tjscheck.exe"),
		path.join(__dirname, "..", "tjscheck.exe")
	];
	for (const candidate of candidates) {
		if (fs.existsSync(candidate)) {
			return candidate;
		}
	}
	// Left bare so the system resolves it on PATH.
	return "tjscheck.exe";
}

function report(document, output) {
	const match = ERROR_LINE.exec(output.trim());
	if (!match) {
		diagnostics.set(document.uri, []);
		return;
	}
	// The parser points at the token it choked on, which is usually the mistake
	// itself; only when the file simply ends early does it land on the last line.
	const line = Math.max(0, parseInt(match[2], 10) - 1);
	const column = Math.max(0, parseInt(match[3], 10) - 1);
	const at = document.lineAt(Math.min(line, document.lineCount - 1));
	const start = new vscode.Position(at.lineNumber, Math.min(column, at.text.length));
	const range = at.text.length > start.character
		? new vscode.Range(start, at.range.end)
		: at.range;
	const diagnostic = new vscode.Diagnostic(range, match[4],
		vscode.DiagnosticSeverity.Error);
	diagnostic.source = "tjscheck";
	diagnostics.set(document.uri, [diagnostic]);
}

let missingCheckerAnnounced = false;

// Once per session, not once per keystroke.
function announceMissingChecker(checker) {
	if (missingCheckerAnnounced) {
		return;
	}
	missingCheckerAnnounced = true;
	vscode.window.showWarningMessage(
		"tjscheck was not found (" + checker + "). TJS files will not be " +
		"checked until tjs.checkerPath points at it.");
}

function check(document) {
	if (document.languageId !== "tjs") {
		return;
	}
	const checker = checkerPath();
	if (!checker) {
		diagnostics.set(document.uri, []);
		return;
	}
	const child = execFile(checker, ["--stdin"], { timeout: 5000 },
		(error, stdout, stderr) => {
			// A checker that cannot be run must say so: silence here is
			// indistinguishable from a file with nothing wrong in it.
			if (error && error.code === "ENOENT") {
				announceMissingChecker(checker);
				diagnostics.set(document.uri, []);
				return;
			}
			report(document, (stdout || "") + (stderr || ""));
		});
	child.stdin.end(document.getText());
}

function schedule(document) {
	const delay = vscode.workspace.getConfiguration("tjscheck").get("debounceMs") || 300;
	const key = document.uri.toString();
	clearTimeout(timers.get(key));
	timers.set(key, setTimeout(() => check(document), delay));
}

function activate(context) {
	diagnostics = vscode.languages.createDiagnosticCollection("tjs");
	context.subscriptions.push(diagnostics);
	context.subscriptions.push(
		vscode.workspace.onDidOpenTextDocument(check),
		vscode.workspace.onDidChangeTextDocument((event) => schedule(event.document)),
		vscode.workspace.onDidSaveTextDocument(check),
		vscode.workspace.onDidCloseTextDocument((document) => {
			diagnostics.delete(document.uri);
			timers.delete(document.uri.toString());
		})
	);
	vscode.workspace.textDocuments.forEach(check);
}

function deactivate() {}

module.exports = { activate, deactivate };
