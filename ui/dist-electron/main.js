import { BrowserWindow as e, app as t, dialog as n, ipcMain as r, safeStorage as i, shell as a } from "electron";
import { spawn as o } from "node:child_process";
import { promises as s } from "node:fs";
import c from "node:os";
import l from "node:path";
import { fileURLToPath as u } from "node:url";
import { EventEmitter as d } from "node:events";
import f from "node:readline";
// electron/core-client.ts
var p = class extends d {
	proc;
	nextId = 1;
	failure = null;
	pending = /* @__PURE__ */ new Map();
	constructor(e, t = []) {
		super(), this.proc = o(e, t, { stdio: [
			"pipe",
			"pipe",
			"pipe"
		] }), this.proc.on("error", (e) => this.fail(/* @__PURE__ */ Error(`core failed to start: ${e.message}`))), this.proc.stdin.on("error", (e) => this.fail(/* @__PURE__ */ Error(`core input failed: ${e.message}`))), f.createInterface({ input: this.proc.stdout }).on("line", (e) => this.onLine(e)), this.proc.stderr.on("data", (e) => console.error("[core]", e.toString().trimEnd())), this.proc.on("exit", (e) => this.fail(/* @__PURE__ */ Error(`core exited (${e})`)));
	}
	fail(e) {
		this.failure = e;
		for (let t of this.pending.values()) t.reject(e);
		this.pending.clear();
	}
	onLine(e) {
		let t;
		try {
			t = JSON.parse(e);
		} catch {
			return;
		}
		if (t.id !== void 0) {
			let e = this.pending.get(t.id);
			if (!e) return;
			this.pending.delete(t.id), t.error ? e.reject(Error(t.error.message)) : e.resolve(t.result);
		} else t.method && this.emit("notification", t.method, t.params);
	}
	call(e, t = {}) {
		if (this.failure) return Promise.reject(this.failure);
		let n = this.nextId++;
		return new Promise((r, i) => {
			this.pending.set(n, {
				resolve: r,
				reject: i
			});
			try {
				this.proc.stdin.write(JSON.stringify({
					jsonrpc: "2.0",
					id: n,
					method: e,
					params: t
				}) + "\n");
			} catch (e) {
				this.pending.delete(n), i(e instanceof Error ? e : Error(String(e)));
			}
		});
	}
	kill() {
		this.proc.kill();
	}
}, m = l.dirname(u(import.meta.url)), h = t.getPath("userData");
function g() {
	return process.platform === "win32" ? l.join(process.env.LOCALAPPDATA || l.join(c.homedir(), "AppData", "Local"), "obsidian-data") : process.platform === "darwin" ? l.join(c.homedir(), "Library", "Application Support", "obsidian-data") : l.join(process.env.XDG_DATA_HOME || l.join(c.homedir(), ".local", "share"), "obsidian-data");
}
var _ = g();
t.setPath("userData", l.join(t.getPath("appData"), "Obsidian Launcher"));
var v = l.join(_, "accounts.json"), y = null, b, x = Promise.resolve(), S = !1, C = null, w = /* @__PURE__ */ new Map();
function T() {
	y = new e({
		width: 1100,
		height: 700,
		webPreferences: {
			preload: l.join(m, "preload.mjs"),
			contextIsolation: !0,
			nodeIntegration: !1
		}
	}), process.env.VITE_DEV_SERVER_URL ? y.loadURL(process.env.VITE_DEV_SERVER_URL) : y.loadFile(l.join(m, "../dist/index.html"));
}
function E(t, n) {
	for (let r of e.getAllWindows()) {
		let e = () => {
			r.isDestroyed() || r.webContents.send("core:event", {
				method: t,
				params: n
			});
		};
		r.webContents.isLoadingMainFrame() ? r.webContents.once("did-finish-load", e) : e();
	}
}
async function D() {
	try {
		return !(!await i.isAsyncEncryptionAvailable() || process.platform === "linux" && i.getSelectedStorageBackend() === "basic_text");
	} catch {
		return !1;
	}
}
async function O() {
	S = await D();
	let e = [v, l.join(h, "accounts.json")];
	for (let t of [...new Set(e)]) {
		let e;
		try {
			e = await s.readFile(t, "utf8");
		} catch {
			continue;
		}
		try {
			let n = JSON.parse(e);
			if (n.format === "obsidian-encrypted-accounts-v1" && typeof n.ciphertext == "string") {
				if (!S) return E("account.storage-warning", { message: "Saved sign-in data is locked because this system has no available OS keyring." }), null;
				let e = await i.decryptStringAsync(Buffer.from(n.ciphertext, "base64")), r = JSON.parse(e.result);
				return e.shouldReEncrypt && (C = t), r;
			}
			if (Array.isArray(n.accounts)) return C = t, n;
		} catch (e) {
			console.error(`[main] could not read saved accounts: ${String(e)}`), E("account.storage-warning", { message: "Saved account data could not be decrypted on this device." });
		}
	}
	return null;
}
async function k() {
	if (!S) return E("account.storage-warning", { message: "This desktop has no OS keyring available. Sign-in will work for this session, but the account will not be saved." }), !1;
	let e = await b.call("accounts.snapshot"), t = await i.encryptStringAsync(JSON.stringify(e));
	await s.mkdir(_, { recursive: !0 });
	let n = `${v}.${process.pid}.tmp`;
	return await s.writeFile(n, JSON.stringify({
		format: "obsidian-encrypted-accounts-v1",
		ciphertext: t.toString("base64")
	}, null, 2), { mode: 384 }), process.platform !== "win32" && await s.chmod(n, 384), await s.rename(n, v), C && C !== v && await s.rm(C, { force: !0 }), C = null, !0;
}
async function A() {
	try {
		return await k();
	} catch (e) {
		return console.error("[main] account persistence failed:", e), E("account.storage-warning", { message: "Could not save the account with the system keyring." }), !1;
	}
}
async function j() {
	await s.mkdir(_, { recursive: !0 });
	let e = t.getPath("userData");
	if (h !== e) for (let t of ["Local Storage", "Session Storage"]) {
		let n = l.join(h, t), r = l.join(e, t);
		try {
			await s.access(r);
		} catch {
			try {
				await s.cp(n, r, {
					recursive: !0,
					errorOnExist: !1,
					force: !1
				});
			} catch (e) {
				if (e.code !== "ENOENT") throw e;
			}
		}
	}
	let n = l.join(h, "instances"), r = l.join(_, "instances");
	try {
		await s.mkdir(r, { recursive: !0 });
		for (let e of await s.readdir(n, { withFileTypes: !0 })) {
			let t = l.join(n, e.name), i = l.join(r, e.name);
			try {
				await s.access(i);
			} catch {
				await s.cp(t, i, {
					recursive: !0,
					errorOnExist: !1,
					force: !1
				});
			}
		}
	} catch (e) {
		if (e.code !== "ENOENT") throw e;
	}
	let i = l.join(h, "version_manifest.json"), a = l.join(_, "meta", "version_manifest.json");
	try {
		await s.access(a);
	} catch {
		try {
			await s.mkdir(l.dirname(a), { recursive: !0 }), await s.copyFile(i, a);
		} catch (e) {
			if (e.code !== "ENOENT") throw e;
		}
	}
}
function M(e) {
	return [
		"auth.offline",
		"accounts.select",
		"accounts.signout",
		"accounts.remove"
	].includes(e);
}
t.whenReady().then(async () => {
	try {
		await j();
	} catch (e) {
		console.error("[main] data migration warning:", e);
	}
	T();
	try {
		let i = process.platform === "win32" ? "launcher_core_cli.exe" : "launcher_core_cli", c = t.isPackaged ? l.join(process.resourcesPath, "core", i) : l.join(m, "../../build/core", i);
		console.log("[main] core path:", c), b = new p(c, ["--data-dir", _]), b.on("notification", (e, t) => {
			e === "auth.complete" && A(), E(e, t);
		}), x = (async () => {
			let e = await O();
			await b.call("accounts.restore", { snapshot: e ?? {
				active: "",
				accounts: []
			} }), (C || e && S && !await s.stat(v).then(() => !0).catch(() => !1)) && await A();
		})(), r.handle("core:call", async (e, t, n) => {
			if (t === "accounts.snapshot" || t === "accounts.restore") throw Error("Account persistence is only available to the main process");
			await x;
			let r = await b.call(t, n);
			return M(t) && await A(), r;
		}), r.handle("game:launch", async (e, t) => {
			if (!t || typeof t.instanceId != "string" || !/^[A-Za-z0-9._-]{1,64}$/.test(t.instanceId)) throw Error("Invalid instance identifier");
			await x;
			let n = w.get(t.instanceId);
			if (n && n.exitCode === null && n.signalCode === null) return {
				started: !1,
				alreadyRunning: !0
			};
			let r = await b.call("game.launchSpec", {
				instanceId: t.instanceId,
				session: t.session
			});
			if (r.instanceId !== t.instanceId || !Array.isArray(r.arguments) || !r.javaPath || !r.workingDirectory) throw Error("The launcher received an invalid game launch configuration");
			let i = l.join(_, "logs", `game-${t.instanceId}.log`);
			await s.mkdir(l.dirname(i), { recursive: !0 });
			let a = await s.open(i, "a", 384);
			await a.appendFile(`\n--- Starting Minecraft ${r.version} at ${(/* @__PURE__ */ new Date()).toISOString()} ---\n`);
			let c;
			try {
				c = o(r.javaPath, r.arguments, {
					cwd: r.workingDirectory,
					env: process.env,
					detached: process.platform !== "win32",
					windowsHide: !0,
					stdio: [
						"ignore",
						a.fd,
						a.fd
					]
				});
			} catch (e) {
				throw await a.close(), e;
			}
			w.set(t.instanceId, c), c.once("error", (e) => {
				w.get(t.instanceId) === c && w.delete(t.instanceId), E("game.launch.status", {
					instanceId: t.instanceId,
					state: "failed",
					message: `Could not start Minecraft: ${e.message}`,
					logPath: i
				});
			}), c.once("close", (e, n) => {
				w.get(t.instanceId) === c && w.delete(t.instanceId), E("game.launch.status", {
					instanceId: t.instanceId,
					state: "exited",
					message: e === 0 ? "Minecraft closed." : `Minecraft exited${e === null ? ` (${n ?? "unknown signal"})` : ` with code ${e}`}. Game log: ${i}`,
					code: e,
					logPath: i
				});
			});
			try {
				await new Promise((e, t) => {
					c.once("spawn", () => e()), c.once("error", t);
				});
			} catch (e) {
				throw w.get(t.instanceId) === c && w.delete(t.instanceId), await a.close(), e;
			}
			return c.unref(), await a.close(), E("game.launch.status", {
				instanceId: t.instanceId,
				state: "running",
				message: `Minecraft ${r.version} started as ${r.username}.`,
				logPath: i
			}), {
				started: !0,
				logPath: i
			};
		}), r.handle("shell:open", (e, t) => {
			t.startsWith("https://") && a.openExternal(t);
		}), r.handle("dialog:pick-file", async (t) => {
			let r = e.fromWebContents(t.sender) ?? void 0, i = await n.showOpenDialog(r, { properties: ["openFile"] });
			return i.canceled ? null : i.filePaths[0] ?? null;
		}), x.catch((e) => {
			console.error("[main] account storage initialization failed:", e), E("account.storage-warning", { message: `Could not open saved accounts: ${String(e)}` });
		});
	} catch (e) {
		console.error("[main] startup error:", e);
	}
}).catch((e) => console.error("[main] whenReady failed:", e)), t.on("before-quit", () => b?.kill()), t.on("window-all-closed", () => t.quit());
export {};
