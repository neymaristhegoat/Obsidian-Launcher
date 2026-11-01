import { app, BrowserWindow, ipcMain } from 'electron'
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
  const exe = process.platform === 'win32' ? 'launcher_core_cli.exe' : 'launcher_core_cli'
  const corePath = app.isPackaged
    ? path.join(process.resourcesPath, 'core', exe)
    : path.join(__dirname, '../../build/core', exe)

  core = new CoreClient(corePath)
  core.on('notification', (method, params) => win?.webContents.send('core:event', { method, params }))

  ipcMain.handle('core:call', (_e, method: string, params: unknown) => core.call(method, params))
  createWindow()
})

app.on('before-quit', () => core?.kill())
app.on('window-all-closed', () => app.quit())