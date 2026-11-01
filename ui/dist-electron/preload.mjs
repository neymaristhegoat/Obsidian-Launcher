let electron = require("electron");
// electron/preload.ts
electron.contextBridge.exposeInMainWorld("core", {
	call: (method, params) => electron.ipcRenderer.invoke("core:call", method, params),
	onEvent: (cb) => {
		const handler = (_, e) => cb(e);
		electron.ipcRenderer.on("core:event", handler);
		return () => electron.ipcRenderer.removeListener("core:event", handler);
	}
});
