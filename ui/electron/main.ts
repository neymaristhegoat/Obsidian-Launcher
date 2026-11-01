import { app, BrowserWindow, ipcMain, shell } from 'electron'
import path from 'node:path'
import { CoreClient } from './core-client'
import { fileURLToPath } from 'node:url' 

const __dirname = path.dirname(fileURLToPath(import.meta.url))
let win: BrowserWindow | null = null
let core: CoreClient

function createWindow() {
  win = new BrowserWindow({
    width: 1100,
    height: 700,
    webPreferences: {
      preload: path.join(__dirname, 'preload.mjs'), // match your output file
      contextIsolation: true,
      nodeIntegration: false,
    },
  })
  if (process.env.VITE_DEV_SERVER_URL) win.loadURL(process.env.VITE_DEV_SERVER_URL)
  else win.loadFile(path.join(__dirname, '../dist/index.html'))
}

app.whenReady().then(() => {
  createWindow()   // window first, so the UI always appears

  try {
    const exe = process.platform === 'win32' ? 'launcher_core_cli.exe' : 'launcher_core_cli'
    const corePath = app.isPackaged
      ? path.join(process.resourcesPath, 'core', exe)
      : path.join(__dirname, '../../build/core', exe)
    console.log('[main] core path:', corePath)

    core = new CoreClient(corePath, ['--data-dir', app.getPath('userData')])
    core.on('notification', (method, params) =>
      win?.webContents.send('core:event', { method, params }))
    ipcMain.handle('core:call', (_e, method: string, params: unknown) => core.call(method, params))
    ipcMain.handle('shell:open', (_e, url: string) => {
      if (url.startsWith('https://')) shell.openExternal(url)
    })
  } catch (e) {
    console.error('[main] startup error:', e)
  }
}).catch((e) => console.error('[main] whenReady failed:', e))

app.on('before-quit', () => core?.kill())
app.on('window-all-closed', () => app.quit())