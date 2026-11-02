import { app, BrowserWindow, dialog, ipcMain, safeStorage, shell } from 'electron'
import { spawn, type ChildProcess } from 'node:child_process'
import { promises as fs } from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { CoreClient } from './core-client'

const __dirname = path.dirname(fileURLToPath(import.meta.url))
const legacyUserData = app.getPath('userData')

function getDataDirectory() {
  if (process.platform === 'win32') {
    return path.join(process.env.LOCALAPPDATA || path.join(os.homedir(), 'AppData', 'Local'), 'obsidian-data')
  }
  if (process.platform === 'darwin')
    return path.join(os.homedir(), 'Library', 'Application Support', 'obsidian-data')
  return path.join(process.env.XDG_DATA_HOME || path.join(os.homedir(), '.local', 'share'), 'obsidian-data')
}

const dataDirectory = getDataDirectory()
app.setPath('userData', path.join(app.getPath('appData'), 'Obsidian Launcher'))
const accountFile = path.join(dataDirectory, 'accounts.json')
let win: BrowserWindow | null = null
let core: CoreClient
let accountInitialization: Promise<void> = Promise.resolve()
let secureAccountStorage = false
let legacyAccountFile: string | null = null
const gameProcesses = new Map<string, ChildProcess>()

function createWindow() {
  win = new BrowserWindow({
    width: 1100,
    height: 700,
    webPreferences: {
      preload: path.join(__dirname, 'preload.mjs'),
      contextIsolation: true,
      nodeIntegration: false,
    },
  })
  if (process.env.VITE_DEV_SERVER_URL) win.loadURL(process.env.VITE_DEV_SERVER_URL)
  else win.loadFile(path.join(__dirname, '../dist/index.html'))
}

function notifyWindows(method: string, params: unknown) {
  for (const browserWindow of BrowserWindow.getAllWindows()) {
    const sendEvent = () => {
      if (!browserWindow.isDestroyed()) browserWindow.webContents.send('core:event', { method, params })
    }
    if (browserWindow.webContents.isLoadingMainFrame()) browserWindow.webContents.once('did-finish-load', sendEvent)
    else sendEvent()
  }
}

async function canPersistAccountSecrets() {
  try {
    if (!await safeStorage.isAsyncEncryptionAvailable()) return false
    // Electron's basic_text fallback uses a hardcoded password, so don't persist tokens with it.
    if (process.platform === 'linux' && safeStorage.getSelectedStorageBackend() === 'basic_text') return false
    return true
  } catch {
    return false
  }
}

type AccountSnapshot = { active?: string; accounts: Array<Record<string, unknown>> }

async function readAccountSnapshot(): Promise<AccountSnapshot | null> {
  secureAccountStorage = await canPersistAccountSecrets()
  const candidates = [accountFile, path.join(legacyUserData, 'accounts.json')]
  for (const candidate of [...new Set(candidates)]) {
    let raw: string
    try { raw = await fs.readFile(candidate, 'utf8') } catch { continue }
    try {
      const stored = JSON.parse(raw) as Record<string, unknown>
      if (stored.format === 'obsidian-encrypted-accounts-v1' && typeof stored.ciphertext === 'string') {
        if (!secureAccountStorage) {
          notifyWindows('account.storage-warning', { message: 'Saved sign-in data is locked because this system has no available OS keyring.' })
          return null
        }
        const decrypted = await safeStorage.decryptStringAsync(Buffer.from(stored.ciphertext, 'base64'))
        const snapshot = JSON.parse(decrypted.result) as AccountSnapshot
        if (decrypted.shouldReEncrypt) legacyAccountFile = candidate
        return snapshot
      }
      // Upgrade a previous plaintext account file in memory, then replace it after the core starts.
      if (Array.isArray(stored.accounts)) {
        legacyAccountFile = candidate
        return stored as unknown as AccountSnapshot
      }
    } catch (error) {
      console.error(`[main] could not read saved accounts: ${String(error)}`)
      notifyWindows('account.storage-warning', { message: 'Saved account data could not be decrypted on this device.' })
    }
  }
  return null
}

async function persistAccountSnapshot() {
  if (!secureAccountStorage) {
    notifyWindows('account.storage-warning', {
      message: 'This desktop has no OS keyring available. Sign-in will work for this session, but the account will not be saved.',
    })
    return false
  }
  const snapshot = await core.call<AccountSnapshot>('accounts.snapshot')
  const ciphertext = await safeStorage.encryptStringAsync(JSON.stringify(snapshot))
  await fs.mkdir(dataDirectory, { recursive: true })
  const temporary = `${accountFile}.${process.pid}.tmp`
  await fs.writeFile(temporary, JSON.stringify({
    format: 'obsidian-encrypted-accounts-v1',
    ciphertext: ciphertext.toString('base64'),
  }, null, 2), { mode: 0o600 })
  if (process.platform !== 'win32') await fs.chmod(temporary, 0o600)
  await fs.rename(temporary, accountFile)
  if (legacyAccountFile && legacyAccountFile !== accountFile)
    await fs.rm(legacyAccountFile, { force: true })
  legacyAccountFile = null
  return true
}

async function persistAccountSnapshotSafely() {
  try { return await persistAccountSnapshot() }
  catch (error) {
    console.error('[main] account persistence failed:', error)
    notifyWindows('account.storage-warning', { message: 'Could not save the account with the system keyring.' })
    return false
  }
}

async function migrateLegacyData() {
  await fs.mkdir(dataDirectory, { recursive: true })
  const newUserData = app.getPath('userData')
  if (legacyUserData !== newUserData) {
    for (const folder of ['Local Storage', 'Session Storage']) {
      const source = path.join(legacyUserData, folder)
      const destination = path.join(newUserData, folder)
      try {
        await fs.access(destination)
      } catch {
        try { await fs.cp(source, destination, { recursive: true, errorOnExist: false, force: false }) }
        catch (error) { if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error }
      }
    }
  }

  const oldInstances = path.join(legacyUserData, 'instances')
  const newInstances = path.join(dataDirectory, 'instances')
  try {
    await fs.mkdir(newInstances, { recursive: true })
    for (const entry of await fs.readdir(oldInstances, { withFileTypes: true })) {
      const source = path.join(oldInstances, entry.name)
      const destination = path.join(newInstances, entry.name)
      try { await fs.access(destination) }
      catch { await fs.cp(source, destination, { recursive: true, errorOnExist: false, force: false }) }
    }
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error
  }

  const oldManifest = path.join(legacyUserData, 'version_manifest.json')
  const newManifest = path.join(dataDirectory, 'meta', 'version_manifest.json')
  try {
    await fs.access(newManifest)
  } catch {
    try {
      await fs.mkdir(path.dirname(newManifest), { recursive: true })
      await fs.copyFile(oldManifest, newManifest)
    } catch (error) {
      if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error
    }
  }
}

function isAccountMutation(method: string) {
  return ['auth.offline', 'accounts.select', 'accounts.signout', 'accounts.remove'].includes(method)
}

app.whenReady().then(async () => {
  try { await migrateLegacyData() }
  catch (error) { console.error('[main] data migration warning:', error) }
  createWindow()

  try {
    const exe = process.platform === 'win32' ? 'launcher_core_cli.exe' : 'launcher_core_cli'
    const corePath = app.isPackaged
      ? path.join(process.resourcesPath, 'core', exe)
      : path.join(__dirname, '../../build/core', exe)
    console.log('[main] core path:', corePath)
    core = new CoreClient(corePath, ['--data-dir', dataDirectory])
    core.on('notification', (method, params) => {
      if (method === 'auth.complete') void persistAccountSnapshotSafely()
      notifyWindows(method, params)
    })

    accountInitialization = (async () => {
      const snapshot = await readAccountSnapshot()
      await core.call('accounts.restore', { snapshot: snapshot ?? { active: '', accounts: [] } })
      if (legacyAccountFile || snapshot && secureAccountStorage && !await fs.stat(accountFile).then(() => true).catch(() => false))
        await persistAccountSnapshotSafely()
    })()

    ipcMain.handle('core:call', async (_event, method: string, params: unknown) => {
      if (method === 'accounts.snapshot' || method === 'accounts.restore')
        throw new Error('Account persistence is only available to the main process')
      await accountInitialization
      const result = await core.call(method, params)
      if (isAccountMutation(method)) await persistAccountSnapshotSafely()
      return result
    })
    ipcMain.handle('game:launch', async (_event, request: { instanceId: string; session: unknown }) => {
      if (!request || typeof request.instanceId !== 'string' || !/^[A-Za-z0-9._-]{1,64}$/.test(request.instanceId))
        throw new Error('Invalid instance identifier')
      await accountInitialization
      const existing = gameProcesses.get(request.instanceId)
      if (existing && existing.exitCode === null && existing.signalCode === null)
        return { started: false, alreadyRunning: true }

      const spec = await core.call<{
        instanceId: string
        version: string
        username: string
        javaPath: string
        workingDirectory: string
        arguments: string[]
      }>('game.launchSpec', { instanceId: request.instanceId, session: request.session })
      if (spec.instanceId !== request.instanceId || !Array.isArray(spec.arguments) || !spec.javaPath || !spec.workingDirectory)
        throw new Error('The launcher received an invalid game launch configuration')

      const logPath = path.join(dataDirectory, 'logs', `game-${request.instanceId}.log`)
      await fs.mkdir(path.dirname(logPath), { recursive: true })
      const logFile = await fs.open(logPath, 'a', 0o600)
      await logFile.appendFile(`\n--- Starting Minecraft ${spec.version} at ${new Date().toISOString()} ---\n`)
      let child: ChildProcess
      try {
        child = spawn(spec.javaPath, spec.arguments, {
          cwd: spec.workingDirectory,
          env: process.env,
          detached: process.platform !== 'win32',
          windowsHide: true,
          stdio: ['ignore', logFile.fd, logFile.fd],
        })
      } catch (error) {
        await logFile.close()
        throw error
      }
      gameProcesses.set(request.instanceId, child)
      child.once('error', (error) => {
        if (gameProcesses.get(request.instanceId) === child) gameProcesses.delete(request.instanceId)
        notifyWindows('game.launch.status', {
          instanceId: request.instanceId,
          state: 'failed',
          message: `Could not start Minecraft: ${error.message}`,
          logPath,
        })
      })
      child.once('close', (code, signal) => {
        if (gameProcesses.get(request.instanceId) === child) gameProcesses.delete(request.instanceId)
        notifyWindows('game.launch.status', {
          instanceId: request.instanceId,
          state: 'exited',
          message: code === 0
            ? 'Minecraft closed.'
            : `Minecraft exited${code === null ? ` (${signal ?? 'unknown signal'})` : ` with code ${code}`}. Game log: ${logPath}`,
          code,
          logPath,
        })
      })
      try {
        await new Promise<void>((resolve, reject) => {
          child.once('spawn', () => resolve())
          child.once('error', reject)
        })
      } catch (error) {
        if (gameProcesses.get(request.instanceId) === child) gameProcesses.delete(request.instanceId)
        await logFile.close()
        throw error
      }
      child.unref()
      await logFile.close()
      notifyWindows('game.launch.status', {
        instanceId: request.instanceId,
        state: 'running',
        message: `Minecraft ${spec.version} started as ${spec.username}.`,
        logPath,
      })
      return { started: true, logPath }
    })
    ipcMain.handle('shell:open', (_event, url: string) => {
      if (url.startsWith('https://')) shell.openExternal(url)
    })
    ipcMain.handle('dialog:pick-file', async (event) => {
      const parent = BrowserWindow.fromWebContents(event.sender) ?? undefined
      const result = await dialog.showOpenDialog(parent, { properties: ['openFile'] })
      return result.canceled ? null : result.filePaths[0] ?? null
    })
    void accountInitialization.catch((error) => {
      console.error('[main] account storage initialization failed:', error)
      notifyWindows('account.storage-warning', { message: `Could not open saved accounts: ${String(error)}` })
    })
  } catch (error) {
    console.error('[main] startup error:', error)
  }
}).catch((error) => console.error('[main] whenReady failed:', error))

app.on('before-quit', () => core?.kill())
app.on('window-all-closed', () => app.quit())
