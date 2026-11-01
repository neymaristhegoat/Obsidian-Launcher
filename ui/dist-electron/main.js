import { BrowserWindow, app, ipcMain, shell } from "electron";
import path from "node:path";
import { spawn } from "node:child_process";
import { EventEmitter } from "node:events";
import readline from "node:readline";
import { fileURLToPath } from "node:url";
// electron/core-client.ts
var CoreClient = class extends EventEmitter {
	proc;
	nextId = 1;
	pending = /* @__PURE__ */ new Map();
	constructor(exePath, args = []) {
		super();
		this.proc = spawn(exePath, [], { stdio: [
			"pipe",
			"pipe",
			"pipe"
		] });
		this.proc.on("error", (err) => console.error("[core] failed to start:", err.message));
		readline.createInterface({ input: this.proc.stdout }).on("line", (line) => this.onLine(line));
		this.proc.stderr.on("data", (d) => console.error("[core]", d.toString().trimEnd()));
		this.proc.on("exit", (code) => {
			for (const p of this.pending.values()) p.reject(/* @__PURE__ */ new Error(`core exited (${code})`));
			this.pending.clear();
		});
	}
	onLine(line) {
		let msg;
		try {
			msg = JSON.parse(line);
		} catch {
			return;
		}
		if (msg.id !== void 0) {
			const p = this.pending.get(msg.id);
			if (!p) return;
			this.pending.delete(msg.id);
			msg.error ? p.reject(new Error(msg.error.message)) : p.resolve(msg.result);
		} else if (msg.method) this.emit("notification", msg.method, msg.params);
	}
	call(method, params = {}) {
		const id = this.nextId++;
		return new Promise((resolve, reject) => {
			this.pending.set(id, {
				resolve,
				reject
			});
			this.proc.stdin.write(JSON.stringify({
				jsonrpc: "2.0",
				id,
				method,
				params
			}) + "\n");
		});
	}
	kill() {
		this.proc.kill();
	}
};
// electron/main.ts
var __dirname = path.dirname(fileURLToPath(import.meta.url));
var win = null;
var core;
function createWindow() {
	win = new BrowserWindow({
		width: 1100,
		height: 700,
		webPreferences: {
			preload: path.join(__dirname, "preload.mjs"),
			contextIsolation: true,
			nodeIntegration: false
		}
	});
	if (process.env.VITE_DEV_SERVER_URL) win.loadURL(process.env.VITE_DEV_SERVER_URL);
	else win.loadFile(path.join(__dirname, "../dist/index.html"));
}
app.whenReady().then(() => {
	createWindow();
	try {
		const exe = process.platform === "win32" ? "launcher_core_cli.exe" : "launcher_core_cli";
		const corePath = app.isPackaged ? path.join(process.resourcesPath, "core", exe) : path.join(__dirname, "../../build/core", exe);
		console.log("[main] core path:", corePath);
		core = new CoreClient(corePath, ["--data-dir", app.getPath("userData")]);
		core.on("notification", (method, params) => win?.webContents.send("core:event", {
			method,
			params
		}));
		ipcMain.handle("core:call", (_e, method, params) => core.call(method, params));
		ipcMain.handle("shell:open", (_e, url) => {
			if (url.startsWith("https://")) shell.openExternal(url);
		});
	} catch (e) {
		console.error("[main] startup error:", e);
	}
}).catch((e) => console.error("[main] whenReady failed:", e));
app.on("before-quit", () => core?.kill());
app.on("window-all-closed", () => app.quit());
export {};
