import { useEffect, useMemo, useRef, useState } from 'react'
import logo from './assets/logo.svg'
import InstanceSetupDialog from './InstanceSetupDialog'
import InstallationDialog, { type InstallationStatus } from './InstallationDialog'

type Page = 'overview' | 'instances' | 'accounts' | 'settings'
type Theme = 'dark' | 'light' | 'system'
type ModLoader = 'vanilla' | 'fabric' | 'forge' | 'neoforge' | 'quilt'
const CLIENT_ID = '6be102b8-2980-4c03-bd3b-3185c27713f4'
interface SavedAccount { id: string; type: 'offline' | 'microsoft'; username: string; uuid: string }
interface Session extends SavedAccount { accessToken: string; offline: boolean; xuid?: string }
interface GameInstance { id: string; name: string; version: string; createdAt: number; loader?: ModLoader; memory?: number; javaPath?: string; installed?: boolean; installedVersion?: string }
interface GameVersion { id: string; type: string; url: string; time: string; releaseTime: string; sha1: string; complianceLevel: number }
interface VersionManifest { versions: GameVersion[]; source: 'network' | 'cache'; fetchedAt: number }
interface Settings { theme: Theme; memory: number; javaPath: string }
interface DeviceCode { userCode: string; verificationUri: string; expiresIn: number }
const versionTimestamp = (version: GameVersion) => Date.parse(version.releaseTime || version.time) || 0

const DEFAULT_SETTINGS: Settings = { theme: 'dark', memory: 4, javaPath: '' }
const readJson = <T,>(key: string, fallback: T): T => {
  try { const raw = localStorage.getItem(key); return raw ? { ...fallback, ...JSON.parse(raw) } : fallback } catch { return fallback }
}
const readSettings = (): Settings => {
  const current = localStorage.getItem('obsidian.settings.v2')
  if (current) {
    try { return { ...DEFAULT_SETTINGS, ...JSON.parse(current) } } catch { return DEFAULT_SETTINGS }
  }
  return { ...readJson('obsidian.settings', DEFAULT_SETTINGS), theme: 'dark' }
}
const errorMessage = (e: unknown) => String((e as Error)?.message ?? e).replace(/^Error invoking remote method '.*?': (Error: )?/, '')

function Icon({ name, size = 18 }: { name: string; size?: number }) {
  const paths: Record<string, string[]> = {
    home: [
      'M15 21v-8a1 1 0 0 0-1-1h-4a1 1 0 0 0-1 1v8',
      'M3 10.3a2 2 0 0 1 .75-1.56l7-5.6a2 2 0 0 1 2.5 0l7 5.6A2 2 0 0 1 21 10.3V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z',
    ],
    package: [
      'M12 22V12',
      'm3.3 7 8.7 5 8.7-5',
      'M8 4.5l8 4.5',
      'M20.7 7.1a2 2 0 0 1 1 1.7v6.4a2 2 0 0 1-1 1.7l-7.7 4.4a2 2 0 0 1-2 0l-7.7-4.4a2 2 0 0 1-1-1.7V8.8a2 2 0 0 1 1-1.7l7.7-4.4a2 2 0 0 1 2 0z',
    ],
    cube: [
      'M12 22V12', 'm3.3 7 8.7 5 8.7-5',
      'M20.7 7.1a2 2 0 0 1 1 1.7v6.4a2 2 0 0 1-1 1.7l-7.7 4.4a2 2 0 0 1-2 0l-7.7-4.4a2 2 0 0 1-1-1.7V8.8a2 2 0 0 1 1-1.7l7.7-4.4a2 2 0 0 1 2 0z',
    ],
    users: [
      'M16 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2',
      'M10 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8Z',
      'M20 8v6', 'M23 11h-6',
    ],
    settings: [
      'M4 21v-7', 'M4 10V3', 'M12 21v-9', 'M12 8V3', 'M20 21v-5', 'M20 12V3',
      'M2 14h4', 'M10 8h4', 'M18 16h4',
    ],
    sliders: [
      'M4 21v-7', 'M4 10V3', 'M12 21v-9', 'M12 8V3', 'M20 21v-5', 'M20 12V3',
      'M2 14h4', 'M10 8h4', 'M18 16h4',
    ],
    plus: ['M12 5v14', 'M5 12h14'],
    arrow: ['M5 12h14', 'm12 5 7 7-7 7'],
    external: ['M15 3h6v6', 'm10 14 11-11', 'M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6'],
    close: ['m18 6-12 12', 'm6 6 12 12'],
    spark: [
      'm12 3 1.9 5.8L20 11l-6.1 2.2L12 19l-1.9-5.8L4 11l6.1-2.2L12 3Z',
      'M19 14v6', 'M16 17h6',
    ],
    sparkles: [
      'm12 3 1.9 5.8L20 11l-6.1 2.2L12 19l-1.9-5.8L4 11l6.1-2.2L12 3Z',
      'M19 14v6', 'M16 17h6', 'M5 3v4', 'M3 5h4',
    ],
    search: ['M11 19a8 8 0 1 1 0-16 8 8 0 0 1 0 16Z', 'm21 21-4.35-4.35'],
    info: ['M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20Z', 'M12 16v-4', 'M12 8h.01'],
    tag: ['M20.6 13.4 11 3.8a2 2 0 0 0-1.4-.6H4a2 2 0 0 0-2 2v5.6a2 2 0 0 0 .6 1.4l9.6 9.6a2 2 0 0 0 2.8 0l5.6-5.6a2 2 0 0 0 0-2.8Z', 'M7 7h.01'],
    check: ['m5 12 4 4L19 6'],
  }
  const iconPaths = paths[name] ?? paths.sparkles
  return <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">{iconPaths.map((d, index) => <path key={`${name}-${index}`} d={d} />)}</svg>
}

const nav: { id: Page; label: string; icon: string }[] = [
  { id: 'overview', label: 'Home', icon: 'home' }, { id: 'instances', label: 'Instances', icon: 'package' },
  { id: 'accounts', label: 'Accounts', icon: 'users' }, { id: 'settings', label: 'Settings', icon: 'sliders' },
]

export default function App() {
  const [page, setPage] = useState<Page>('overview')
  const [configInstanceId, setConfigInstanceId] = useState('')
  const [showInstanceSetup, setShowInstanceSetup] = useState(false)
  const [installingInstance, setInstallingInstance] = useState<GameInstance | null>(null)
  const [installationStatus, setInstallationStatus] = useState<InstallationStatus>('installing')
  const [installationProgress, setInstallationProgress] = useState(0)
  const [installationLogs, setInstallationLogs] = useState<string[]>([])
  const installInstanceId = useRef('')
  const [saved, setSaved] = useState<SavedAccount[]>([])
  const [session, setSession] = useState<Session | null>(null)
  const [deviceCode, setDeviceCode] = useState<DeviceCode | null>(null)
  const [status, setStatus] = useState('')
  const [authStarting, setAuthStarting] = useState(false)
  const [launchingInstanceId, setLaunchingInstanceId] = useState('')
  const [authReady, setAuthReady] = useState(false)
  const [offlineName, setOfflineName] = useState('')
  const [settings, setSettings] = useState<Settings>(readSettings)
  const [settingsReady, setSettingsReady] = useState(false)
  const [versionManifest, setVersionManifest] = useState<VersionManifest | null>(null)
  const [manifestLoading, setManifestLoading] = useState(true)
  const [manifestError, setManifestError] = useState('')
  const [instances, setInstances] = useState<GameInstance[]>([])
  const [instancesReady, setInstancesReady] = useState(false)
  const [selectedInstanceId, setSelectedInstanceId] = useState(() => localStorage.getItem('obsidian.selectedInstance') ?? '')
  const [showCreate, setShowCreate] = useState(false)
  const [creatingInstance, setCreatingInstance] = useState(false)
  const [editingInstance, setEditingInstance] = useState<GameInstance | null>(null)
  const [deletingInstance, setDeletingInstance] = useState<GameInstance | null>(null)
  const [deletingBusy, setDeletingBusy] = useState(false)
  const [deleteError, setDeleteError] = useState('')
  const [newName, setNewName] = useState('')
  const [newVersion, setNewVersion] = useState('')
  const [showSnapshots, setShowSnapshots] = useState(false)
  const [filter, setFilter] = useState('')
  const [selectingJava, setSelectingJava] = useState(false)
  const startupRequest = useRef<Promise<{ active: string; accounts: SavedAccount[] }> | null>(null)
  const manifestRequest = useRef<Promise<VersionManifest> | null>(null)
  const startupHandled = useRef(false)
  const profileSaveTimer = useRef<number | null>(null)

  useEffect(() => {
    let cancelled = false
    void window.core.call<{ settings?: Settings | null }>('config.read').then((stored) => {
      if (cancelled) return
      if (stored.settings && typeof stored.settings === 'object')
        setSettings({ ...DEFAULT_SETTINGS, ...stored.settings })
      setSettingsReady(true)
    }).catch((error) => {
      if (cancelled) return
      setStatus(`Could not load settings: ${errorMessage(error)}`)
      setSettingsReady(true)
    })
    return () => { cancelled = true }
  }, [])
  useEffect(() => {
    let cancelled = false
    const legacy = (() => {
      try { return JSON.parse(localStorage.getItem('obsidian.instances') ?? '[]') as GameInstance[] } catch { return [] }
    })()
    void window.core.call<GameInstance[]>('instances.list').then((stored) => {
      if (cancelled) return
      setInstances(stored.length ? stored : legacy)
      setInstancesReady(true)
    }).catch((error) => {
      if (cancelled) return
      setInstances(legacy)
      setInstancesReady(true)
      setStatus(`Could not load instance profiles: ${errorMessage(error)}`)
    })
    return () => { cancelled = true }
  }, [])
  useEffect(() => {
    if (!settingsReady) return
    localStorage.setItem('obsidian.settings.v2', JSON.stringify(settings))
    void window.core.call('config.write', { settings }).catch((error) => setStatus(`Could not save settings: ${errorMessage(error)}`))
  }, [settings, settingsReady])
  useEffect(() => {
    if (!instancesReady) return
    localStorage.setItem('obsidian.instances', JSON.stringify(instances))
    if (profileSaveTimer.current !== null) window.clearTimeout(profileSaveTimer.current)
    profileSaveTimer.current = window.setTimeout(() => {
      for (const profile of instances)
        void window.core.call('instances.save', { profile }).catch((error) => setStatus(`Could not save ${profile.name}: ${errorMessage(error)}`))
    }, 250)
    return () => {
      if (profileSaveTimer.current !== null) window.clearTimeout(profileSaveTimer.current)
    }
  }, [instances, instancesReady])
  useEffect(() => {
    const syncFromAnotherWindow = (event: StorageEvent) => {
      if (event.key === 'obsidian.instances' && event.newValue) {
        try { setInstances(JSON.parse(event.newValue) as GameInstance[]) } catch { /* Ignore invalid external state. */ }
      } else if (event.key === 'obsidian.selectedInstance' && event.newValue !== null) {
        setSelectedInstanceId(event.newValue)
      }
    }
    window.addEventListener('storage', syncFromAnotherWindow)
    return () => window.removeEventListener('storage', syncFromAnotherWindow)
  }, [])
  const selectedInstance = instances.find((instance) => instance.id === selectedInstanceId) ?? instances[0] ?? null
  useEffect(() => { localStorage.setItem('obsidian.selectedInstance', selectedInstance?.id ?? '') }, [selectedInstance?.id])
  useEffect(() => {
    const applyTheme = () => {
      const theme = settings.theme === 'system'
        ? (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light')
        : settings.theme
      document.documentElement.dataset.theme = theme
      document.documentElement.style.colorScheme = theme
    }
    applyTheme()
    const preference = window.matchMedia('(prefers-color-scheme: dark)')
    preference.addEventListener('change', applyTheme)
    return () => preference.removeEventListener('change', applyTheme)
  }, [settings.theme])

  async function loadVersionManifest() {
    if (manifestRequest.current) return
    setManifestLoading(true)
    setManifestError('')
    const request = window.core.call<VersionManifest>('versions.list')
    manifestRequest.current = request
    try {
      const result = await request
      if (!result || !Array.isArray(result.versions) || result.versions.length === 0) {
        throw new Error('The Minecraft version manifest did not contain any versions.')
      }
      setVersionManifest(result)
      const latestRelease = [...result.versions].filter((version) => version.type === 'release').sort((a, b) => versionTimestamp(b) - versionTimestamp(a))[0]
      setNewVersion((current) => current || latestRelease?.id || result.versions[0].id)
    } catch (e) {
      setManifestError(errorMessage(e))
    } finally {
      if (manifestRequest.current === request) manifestRequest.current = null
      setManifestLoading(false)
    }
  }

  useEffect(() => { void loadVersionManifest() }, [])

  async function loadSaved() {
    const result = await window.core.call<{ active: string; accounts: SavedAccount[] }>('accounts.list')
    setSaved(result.accounts)
    return result
  }
  async function selectAccount(account: SavedAccount) {
    setStatus(`Connecting ${account.username}…`)
    try {
      const active = await window.core.call<Session>('accounts.select', { id: account.id, clientId: CLIENT_ID })
      setSession(active); setStatus(''); setAuthReady(true)
    } catch (e) { setSession(null); setStatus(errorMessage(e)); setAuthReady(true) }
  }

  useEffect(() => {
    const unsubscribe = window.core.onEvent((event) => {
      if (event.method === 'auth.complete') {
        setDeviceCode(null); setSession(event.params as Session); setStatus(''); setAuthReady(true); void loadSaved()
      } else if (event.method === 'auth.failed') {
        setDeviceCode(null); setStatus(`Sign-in failed: ${(event.params as { message: string }).message}`); setAuthReady(true)
      } else if (event.method === 'account.storage-warning') {
        setStatus((event.params as { message: string }).message)
      } else if (event.method === 'game.launch.status') {
        const update = event.params as { instanceId: string; message: string }
        setStatus(update.message)
      } else if (event.method === 'instances.install.progress') {
        const update = event.params as { instanceId: string; message: string; percent: number; state: InstallationStatus }
        if (update.instanceId !== installInstanceId.current) return
        const time = new Date().toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })
        setInstallationLogs((current) => [...current.slice(-399), `[${time}] ${update.message}`])
        if ((update.state === 'installing' || update.state === 'complete') && Number.isFinite(update.percent))
          setInstallationProgress(Math.max(0, Math.min(100, update.percent)))
        if (['installing', 'cancelling', 'complete', 'failed', 'cancelled'].includes(update.state))
          setInstallationStatus((current) => current === 'cancelling' && update.state === 'installing' ? current : update.state)
      }
    })
    startupRequest.current ??= window.core.call<{ active: string; accounts: SavedAccount[] }>('accounts.list')
    void startupRequest.current.then((result) => {
      if (startupHandled.current) return
      startupHandled.current = true
      setSaved(result.accounts)
      const active = result.accounts.find((account) => account.id === result.active)
      if (active) void selectAccount(active)
      else setAuthReady(true)
    }).catch((e) => { if (!startupHandled.current) { startupHandled.current = true; setStatus(`Core connection failed: ${errorMessage(e)}`); setAuthReady(true) } })
    return unsubscribe
  }, [])

  async function signInMicrosoft() {
    if (authStarting || deviceCode) return
    setAuthStarting(true)
    setStatus('Opening Microsoft sign-in…')
    try {
      const result = await window.core.call<DeviceCode>('auth.start', { clientId: CLIENT_ID })
      setDeviceCode(result); setStatus('')
      await window.core.openExternal(result.verificationUri)
    } catch (e) { setStatus(errorMessage(e)) }
    finally { setAuthStarting(false) }
  }
  async function addOffline() {
    try {
      const result = await window.core.call<Session>('auth.offline', { username: offlineName.trim() })
      setSession(result); setOfflineName(''); setStatus(''); setAuthReady(true); await loadSaved()
    } catch (e) { setStatus(errorMessage(e)) }
  }
  async function signOut() {
    try { await window.core.call('accounts.signout'); setSession(null); setStatus('You are signed out.') }
    catch (e) { setStatus(errorMessage(e)) }
  }
  async function forget(account: SavedAccount) {
    try {
      await window.core.call('accounts.remove', { id: account.id })
      if (session?.id === account.id) setSession(null)
      await loadSaved()
    } catch (e) { setStatus(errorMessage(e)) }
  }
  async function cancelSignIn() {
    try { await window.core.call('auth.cancel') } catch (e) { setStatus(errorMessage(e)) }
    setDeviceCode(null)
  }
  async function chooseJava() {
    setSelectingJava(true)
    try {
      const path = await window.core.pickFile()
      if (path) setSettings((current) => ({ ...current, javaPath: path }))
    } catch (e) { setStatus(errorMessage(e)) }
    finally { setSelectingJava(false) }
  }
  async function createInstance(event: React.FormEvent<HTMLFormElement>) {
    event.preventDefault()
    if (creatingInstance) return
    const name = newName.trim()
    if (!name) return
    setCreatingInstance(true)
    try {
      if (editingInstance) {
        const updated = { ...editingInstance, name }
        await window.core.call('instances.save', { profile: updated })
        setInstances((current) => current.map((item) => item.id === updated.id ? updated : item))
        setEditingInstance(null)
      } else {
        const created: GameInstance = { id: crypto.randomUUID(), name, version: newVersion, createdAt: Date.now(), loader: 'vanilla', memory: settings.memory, javaPath: settings.javaPath }
        await window.core.call('instances.save', { profile: created })
        const nextInstances = [created, ...instances]
        localStorage.setItem('obsidian.instances', JSON.stringify(nextInstances))
        localStorage.setItem('obsidian.selectedInstance', created.id)
        setInstances(nextInstances)
        setSelectedInstanceId(created.id)
        setConfigInstanceId(created.id)
        setShowInstanceSetup(true)
        setPage('instances')
      }
      setNewName('')
      setShowCreate(false)
    } catch (error) {
      setStatus(`Could not save instance: ${errorMessage(error)}`)
    } finally {
      setCreatingInstance(false)
    }
  }

  function openInstanceConfig(instance: GameInstance) {
    setSelectedInstanceId(instance.id)
    localStorage.setItem('obsidian.selectedInstance', instance.id)
    setConfigInstanceId(instance.id)
    setShowInstanceSetup(true)
  }

  function startInstanceInstallation(instance: GameInstance) {
    installInstanceId.current = instance.id
    setShowInstanceSetup(false)
    setInstallingInstance(instance)
    setInstallationStatus('installing')
    setInstallationProgress(0)
    setInstallationLogs([`Starting Minecraft ${instance.version} installation for ${instance.name}.`])
    void window.core.call('instances.install', { instanceId: instance.id }).then(() => {
      setInstallationStatus('complete')
      setInstallationProgress(100)
      updateInstance(instance.id, { installed: true, installedVersion: instance.version })
    }).catch((error) => {
      const message = errorMessage(error)
      const cancelled = message.toLowerCase().includes('cancel')
      setInstallationStatus(cancelled ? 'cancelled' : 'failed')
      setInstallationLogs((current) => current.at(-1)?.includes(message)
        ? current
        : [...current.slice(-399), `${cancelled ? 'Cancelled' : 'Error'}: ${message}`])
    })
  }

  async function launchInstance(instance: GameInstance) {
    if (launchingInstanceId) return
    if (instance.installedVersion !== instance.version) {
      openInstanceConfig(instance)
      setStatus(`Install Minecraft ${instance.version} before launching this profile.`)
      return
    }
    if (!session) {
      setStatus('Sign in or add an offline username before launching Minecraft.')
      return
    }

    setLaunchingInstanceId(instance.id)
    setStatus(session.offline ? `Preparing Minecraft for ${session.username}…` : 'Checking your Microsoft account…')
    try {
      // Refresh Microsoft credentials immediately before launch. If refresh fails, report it
      // instead of silently launching with an offline profile.
      const launchSession = session.offline
        ? session
        : await window.core.call<Session>('accounts.select', { id: session.id, clientId: CLIENT_ID })
      if (!session.offline) setSession(launchSession)
      const result = await window.core.launchGame({ instanceId: instance.id, session: launchSession })
      if (result.alreadyRunning) setStatus(`${instance.name} is already running.`)
      else setStatus(`Starting Minecraft ${instance.version} as ${launchSession.username}…`)
    } catch (error) {
      setStatus(`Could not launch Minecraft: ${errorMessage(error)}`)
    } finally {
      setLaunchingInstanceId('')
    }
  }

  function cancelInstanceInstallation() {
    if (!installingInstance || installationStatus !== 'installing') return
    setInstallationStatus('cancelling')
    void window.core.call<{ requested: boolean }>('instances.install.cancel', { instanceId: installingInstance.id }).then((result) => {
      if (!result.requested) setInstallationStatus((current) => current === 'cancelling' ? 'complete' : current)
    }).catch((error) => {
      setInstallationLogs((current) => [...current.slice(-399), `Could not cancel: ${errorMessage(error)}`])
      setInstallationStatus('installing')
    })
  }

  function closeInstallationDialog() {
    installInstanceId.current = ''
    setInstallingInstance(null)
  }

  function updateInstance(instanceId: string, changes: Partial<GameInstance>) {
    const nextInstances = instances.map((instance) => instance.id === instanceId ? { ...instance, ...changes } : instance)
    localStorage.setItem('obsidian.instances', JSON.stringify(nextInstances))
    setInstances(nextInstances)
  }

  async function confirmDeleteInstance() {
    if (!deletingInstance || deletingBusy) return
    setDeletingBusy(true)
    setDeleteError('')
    try {
      await window.core.call('instances.delete', { instanceId: deletingInstance.id })
      const nextInstances = instances.filter((item) => item.id !== deletingInstance.id)
      localStorage.setItem('obsidian.instances', JSON.stringify(nextInstances))
      setInstances(nextInstances)
      setDeletingInstance(null)
    } catch (e) {
      setDeleteError(errorMessage(e))
    } finally {
      setDeletingBusy(false)
    }
  }

  const filteredInstances = useMemo(() => instances.filter((instance) => `${instance.name} ${instance.version}`.toLowerCase().includes(filter.toLowerCase())), [instances, filter])
  const availableVersions = useMemo(() => (versionManifest?.versions ?? [])
    .filter((version) => version.type === 'release' || (showSnapshots && version.type === 'snapshot'))
    .sort((a, b) => versionTimestamp(b) - versionTimestamp(a)), [versionManifest, showSnapshots])
  const configuredInstance = instances.find((instance) => instance.id === configInstanceId) ?? null
  const title = nav.find((item) => item.id === page)?.label ?? 'Home'

  useEffect(() => {
    const isOpen = Boolean(deviceCode || showCreate || deletingInstance || showInstanceSetup || installingInstance)
    if (!isOpen) return
    const dialog = document.querySelector<HTMLElement>('[role="dialog"]')
    const focusable = dialog?.querySelectorAll<HTMLElement>('a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])')
    const previous = document.activeElement as HTMLElement | null
    const initialFocus = dialog?.querySelector<HTMLElement>('[data-dialog-autofocus]') ?? focusable?.[0]
    initialFocus?.focus()
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === 'Escape') {
        if (deviceCode) void cancelSignIn()
        else if (showCreate) { setShowCreate(false); setEditingInstance(null); setNewName('') }
        else if (showInstanceSetup) setShowInstanceSetup(false)
        else if (installingInstance && ['complete', 'failed', 'cancelled'].includes(installationStatus)) closeInstallationDialog()
        else if (!deletingBusy) setDeletingInstance(null)
      }
      const currentFocusable = dialog?.querySelectorAll<HTMLElement>('a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), [tabindex]:not([tabindex="-1"])')
      if (event.key === 'Tab' && currentFocusable?.length) {
        const first = currentFocusable[0]
        const last = currentFocusable[currentFocusable.length - 1]
        if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus() }
        else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus() }
      }
    }
    window.addEventListener('keydown', onKeyDown)
    return () => { window.removeEventListener('keydown', onKeyDown); previous?.focus() }
  }, [deviceCode, showCreate, showInstanceSetup, installingInstance, installationStatus, deletingInstance, deletingBusy])

  if (!authReady) return <main className="auth-screen" aria-label="Obsidian Launcher sign in"><div className="auth-loading-card" role="status" aria-live="polite"><span className="auth-logo"><img src={logo} alt="" /></span><b>OBSIDIAN</b><span className="loading-line"/><small>{status || 'Checking your account…'}</small></div></main>
  if (!session) return <LoginScreen saved={saved} offlineName={offlineName} setOfflineName={setOfflineName} status={status} authStarting={authStarting} signInMicrosoft={() => void signInMicrosoft()} addOffline={() => void addOffline()} onSelectAccount={(account) => void selectAccount(account)} deviceCode={deviceCode} cancelSignIn={() => void cancelSignIn()} />

  return <div className="app-shell">
    <a className="skip-link" href="#main-content">Skip to content</a>
    <aside className="sidebar" aria-label="Launcher sidebar">
      <button className="brand" onClick={() => setPage('overview')} aria-label="Go to Home"><span className="brand-mark"><img src={logo} alt="" /></span><span className="brand-copy"><b>OBSIDIAN</b><small>LAUNCHER</small></span></button>
      <div className="side-label">WORKSPACE</div>
      <nav className="side-nav" aria-label="Main navigation">{nav.map((item) => <button key={item.id} aria-label={item.label} title={item.label} aria-current={page === item.id ? 'page' : undefined} className={`nav-item ${page === item.id ? 'selected' : ''}`} onClick={() => { setPage(item.id); setStatus('') }}><Icon name={item.icon} /><span>{item.label}</span>{item.id === 'instances' && instances.length > 0 && <small className="nav-count">{instances.length}</small>}</button>)}</nav>
      <div className="sidebar-bottom">
        <div className="sidebar-tip"><span className="tip-icon"><Icon name="package" size={16} /></span><b>Separate game setups</b><p>Create a profile for each version or mod collection.</p><button onClick={() => { setPage('instances'); setShowCreate(true) }}>Create instance <Icon name="arrow" size={14} /></button></div>
        <button className="profile-chip" aria-label={`Account: ${session.username}`} title={`Account: ${session.username}`} onClick={() => setPage('accounts')}><span className="avatar" aria-hidden="true">{session.username.slice(0, 1).toUpperCase()}</span><span className="profile-text"><b>{session.username}</b><small>{session.offline ? 'Offline account' : 'Microsoft account'}</small></span><span className={`online-dot ${session ? 'on' : ''}`} aria-hidden="true" /></button>
      </div>
    </aside>

    <main className="main-area" id="main-content" tabIndex={-1}>
      <header className="topbar"><div className="breadcrumb"><span>Obsidian</span><i aria-hidden="true">/</i><b>{title}</b></div><div className="topbar-actions"><span className="build-badge"><span aria-hidden="true" />Desktop client</span><button className="icon-button" aria-label="Open Settings" title="Settings" onClick={() => setPage('settings')}><Icon name="sliders" /></button></div></header>
      <div className="page-content">
        {page === 'overview' && <>
          <section className="launcher-welcome" aria-labelledby="home-title">
            <div><div className="eyebrow"><span className="eyebrow-line" />OBSIDIAN LAUNCHER</div><h1 id="home-title">Ready when<br className="welcome-break" /> <em>you are.</em></h1><p className="lead">Choose a profile and keep your Minecraft setups in one place.</p></div>
            <div className="welcome-account"><span className="overview-avatar" aria-hidden="true">{session.username.slice(0, 1).toUpperCase()}</span><span><small>PLAYING AS</small><b>{session.username}</b></span><button className="overview-manage" onClick={() => setPage('accounts')} aria-label="Manage account">Manage <Icon name="arrow" size={14}/></button></div>
          </section>
          <section className="launcher-home-grid" aria-label="Launcher home">
            <article className="launch-feature" aria-labelledby="selected-profile-title">
              <div className="launch-feature-art" aria-hidden="true"><span className="launch-orbit launch-orbit-one"/><span className="launch-orbit launch-orbit-two"/><span className="launch-emblem"><img src={logo} alt="" /></span><span className="launch-spark launch-spark-one">✦</span><span className="launch-spark launch-spark-two">✧</span></div>
              <div className="launch-feature-content">
                <div className="launch-feature-top"><span className="launch-kicker"><i/> SELECTED INSTANCE</span>{selectedInstance && <span className="launch-version">Minecraft {selectedInstance.version}</span>}</div>
                {selectedInstance ? <><h2 id="selected-profile-title">{selectedInstance.name}</h2><p className="launch-description">{selectedInstance.installedVersion === selectedInstance.version ? `Minecraft ${selectedInstance.version} is installed for this profile.` : 'Your selected profile is ready to configure.'}</p><div className="launch-notice"><Icon name="info" size={15}/><span>{selectedInstance.installedVersion === selectedInstance.version ? `Play as ${session.username} · ${session.offline ? 'Offline profile' : 'Microsoft account'}` : 'Install this Minecraft version from the instance setup.'}</span></div><div className="launch-actions"><button className="button primary" onClick={() => void launchInstance(selectedInstance)} disabled={launchingInstanceId === selectedInstance.id}>{launchingInstanceId === selectedInstance.id ? 'Starting…' : selectedInstance.installedVersion === selectedInstance.version ? 'Launch game' : 'Install version'} <Icon name="arrow" size={15}/></button><button className="button launch-secondary" onClick={() => openInstanceConfig(selectedInstance)}>Configure <Icon name="settings" size={16}/></button><button className="button launch-secondary" onClick={() => { setPage('instances'); setShowCreate(true) }}><Icon name="plus" size={16}/> New instance</button></div></> : <><h2 id="selected-profile-title">Create your first profile</h2><p className="launch-description">Set up a version profile to start organizing your Minecraft library.</p><div className="launch-actions"><button className="button primary" onClick={() => { setPage('instances'); setShowCreate(true) }}><Icon name="plus" size={16}/> Create instance</button><button className="button launch-secondary" onClick={() => setPage('settings')}><Icon name="settings" size={16}/> Launcher settings</button></div></>}
              </div>
              <div className="feature-footer"><span><Icon name="package" size={15}/>{instances.length} {instances.length === 1 ? 'profile' : 'profiles'} in your library</span><button onClick={() => { setFilter(''); setPage('instances') }}>Browse library <Icon name="arrow" size={14}/></button></div>
            </article>
            <aside className="launcher-rail">
              <section className="rail-card account-rail"><div className="rail-card-heading"><span className="rail-icon"><Icon name="users" size={17}/></span><span><small>ACCOUNT</small><b>{session.offline ? 'Offline profile' : 'Microsoft account'}</b></span></div><div className="rail-account-name"><span className="overview-avatar" aria-hidden="true">{session.username.slice(0, 1).toUpperCase()}</span><strong>{session.username}</strong><i className="online-dot on" aria-label="Connected"/></div><button className="rail-link" onClick={() => setPage('accounts')}>Account settings <Icon name="arrow" size={14}/></button></section>
              <section className="rail-card library-rail"><div className="rail-card-heading"><span className="rail-icon violet"><Icon name="package" size={17}/></span><span><small>YOUR LIBRARY</small><b>{instances.length} saved {instances.length === 1 ? 'instance' : 'instances'}</b></span></div><p>{selectedInstance ? `${selectedInstance.name} is selected · Minecraft ${selectedInstance.version}` : 'Create profiles for different versions and setups.'}</p><button className="rail-link" onClick={() => { setFilter(''); setPage('instances') }}>Open instances <Icon name="arrow" size={14}/></button></section>
              <section className="rail-card settings-rail"><div className="settings-rail-copy"><span className="rail-icon"><Icon name="settings" size={17}/></span><span><b>Launcher settings</b><small>Appearance, Java, memory</small></span></div><button className="rail-open" onClick={() => setPage('settings')} aria-label="Open launcher settings"><Icon name="arrow" size={16}/></button></section>
            </aside>
          </section>
          <section className="home-library panel"><div className="panel-heading"><div><h2>Instances</h2><p>Switch between your saved profiles.</p></div><button className="text-button" onClick={() => { setFilter(''); setPage('instances') }}>View all <Icon name="arrow" size={14}/></button></div>
            {instances.length === 0 ? <div className="home-empty"><span className="empty-cube"><Icon name="cube" size={20}/></span><span><b>Your library is empty</b><small>Create an instance profile to choose a game version.</small></span><button className="button subtle" onClick={() => { setPage('instances'); setShowCreate(true) }}>Create profile <Icon name="arrow" size={14}/></button></div> : <div className="home-profile-list">{instances.slice(0, 4).map((instance) => <button key={instance.id} className={`home-profile-row ${selectedInstance?.id === instance.id ? 'active' : ''}`} aria-pressed={selectedInstance?.id === instance.id} onClick={() => setSelectedInstanceId(instance.id)}><span className="mini-world"><Icon name="cube" size={18}/></span><span className="home-profile-name"><b>{instance.name}</b><small>Minecraft {instance.version}</small></span>{selectedInstance?.id === instance.id ? <span className="selected-pill"><Icon name="check" size={13}/> Selected</span> : <span className="profile-select-label">Select</span>}<Icon name="arrow" size={14}/></button>)}</div>}
          </section>
        </>}

        {page === 'instances' && <><PageHeading eyebrow="YOUR LIBRARY" title="Instances" description="Keep separate game versions and setups organized." action={<button className="button primary" onClick={() => setShowCreate(true)}><Icon name="plus" size={17} /> New instance</button>} />
          <div className="instance-toolbar"><label className="search-box"><Icon name="search" size={16} /><span className="visually-hidden">Search instances</span><input aria-label="Search instances" value={filter} onChange={(e) => setFilter(e.target.value)} placeholder="Find an instance…" /></label><span className="result-count" aria-live="polite">{filteredInstances.length} {filteredInstances.length === 1 ? 'instance' : 'instances'}</span></div>
          {filteredInstances.length === 0 ? <div className="empty-state"><div className="empty-orb"><Icon name="cube" size={30} /></div><h2>{instances.length ? 'No matching instances' : 'No instance profiles yet'}</h2><p>{instances.length ? 'Try another name or version.' : 'Create a profile to choose a Minecraft version and organize separate setups.'}</p>{!instances.length && <button className="button primary" onClick={() => setShowCreate(true)}><Icon name="plus" size={17} /> Create instance</button>}<small>Profiles are saved locally. Game launches use the selected Microsoft account or offline username.</small></div> : <div className="instance-grid">{filteredInstances.map((instance) => { const isInstalled = instance.installedVersion === instance.version; return <article className={`instance-card ${selectedInstance?.id === instance.id ? 'selected-instance-card' : ''}`} key={instance.id}><div className="instance-cover"><div className="cover-mark" aria-hidden="true"><Icon name="package" size={42} /></div><span className="version-tag">{instance.version}</span><span className="not-installed">{isInstalled ? 'INSTALLED' : 'NOT INSTALLED'}</span></div><div className="instance-info"><div><h3>{instance.name}{selectedInstance?.id === instance.id && <span className="instance-current">SELECTED</span>}</h3><p>Minecraft {instance.version} · Added {new Date(instance.createdAt).toLocaleDateString()}</p></div><div className="instance-actions">{isInstalled ? <><button className="button primary compact" disabled={launchingInstanceId === instance.id} onClick={() => void launchInstance(instance)}>{launchingInstanceId === instance.id ? 'Starting…' : 'Launch'}</button><button className="button subtle compact" onClick={() => openInstanceConfig(instance)}>Configure</button></> : <button className="button subtle compact" onClick={() => openInstanceConfig(instance)}>Install</button>}{selectedInstance?.id !== instance.id && <button className="button subtle compact" onClick={() => setSelectedInstanceId(instance.id)}>Select</button>}<button className="button subtle compact" onClick={() => { setEditingInstance(instance); setNewName(instance.name); setShowCreate(true) }}>Rename</button><button className="button subtle compact danger-button" onClick={() => { setDeleteError(''); setDeletingInstance(instance) }}>Delete</button></div></div></article>})}</div>}
        </>}

        {page === 'accounts' && <><PageHeading eyebrow="YOUR IDENTITY" title="Accounts" description="Choose how you want to show up in your worlds." />
          {session && <div className="active-account"><div className="account-avatar">{session.username.slice(0, 1).toUpperCase()}</div><div className="account-detail"><span className="active-label"><i /> ACTIVE ACCOUNT</span><h2>{session.username}</h2><p>{session.offline ? 'Offline profile' : 'Microsoft account'} <span>·</span> UUID {session.uuid}</p></div><button className="button subtle" onClick={() => void signOut()}>Sign out</button></div>}
          <div className="account-options"><article className="account-option microsoft-option"><div className="option-icon microsoft-icon" aria-hidden="true"><span className="ms-mark"><i/><i/><i/><i/></span></div><div className="option-copy"><span className="eyebrow">RECOMMENDED</span><h3>Microsoft account</h3><p>Sign in to use your Minecraft: Java Edition profile. Your account stays saved on this device.</p><button className="button primary" onClick={() => void signInMicrosoft()}>Continue with Microsoft <Icon name="arrow" size={15} /></button></div></article><article className="account-option offline-option"><div className="option-icon offline-icon"><Icon name="users" size={20} /></div><div className="option-copy"><span className="eyebrow">LOCAL PROFILE</span><h3>Offline account</h3><p>Use a local username. Offline profiles cannot access online or authenticated servers.</p><form className="offline-form" onSubmit={(e) => { e.preventDefault(); void addOffline() }}><input value={offlineName} onChange={(e) => setOfflineName(e.target.value)} placeholder="Choose a username" maxLength={16} minLength={3} required /><button className="button secondary" type="submit">Add profile</button></form></div></article></div>
          {saved.length > 0 && <section className="saved-section"><div className="section-heading"><div><h2>Saved profiles</h2><p>Switch between accounts saved on this device.</p></div></div><div className="saved-list">{saved.map((account) => <div className="saved-account" key={account.id}><span className="avatar" aria-hidden="true">{account.username.slice(0, 1).toUpperCase()}</span><div><b>{account.username}</b><small>{account.type === 'offline' ? 'Offline profile' : 'Microsoft account'}</small></div><div className="saved-actions">{session?.id === account.id ? <span className="current-pill">Current</span> : <button className="button subtle" onClick={() => void selectAccount(account)}>Switch to {account.username}</button>}<button className="remove-button" aria-label={`Forget ${account.username}`} title={`Forget ${account.username}`} onClick={() => void forget(account)}><Icon name="close" size={16} /></button></div></div>)}</div></section>}
        </>}

        {page === 'settings' && <><PageHeading eyebrow="MAKE IT YOURS" title="Settings" description="Tune the launcher to fit your setup." />
          <div className="settings-layout"><div className="settings-main"><SettingsGroup title="Appearance" description="Choose how Obsidian looks on this device."><div className="setting-row"><div><b>Color theme</b><small>Set the look of the launcher.</small></div><div className="segmented" role="group" aria-label="Color theme">{(['dark', 'light', 'system'] as Theme[]).map((theme) => <button key={theme} type="button" aria-pressed={settings.theme === theme} className={settings.theme === theme ? 'active' : ''} onClick={() => setSettings((current) => ({ ...current, theme }))}>{theme[0].toUpperCase() + theme.slice(1)}</button>)}</div></div></SettingsGroup>
            <SettingsGroup title="Game" description="Default preferences for new instances."><div className="setting-row"><div><b>Memory allocation</b><small>Recommended: 4 GB for most setups.</small></div><div className="select-wrap"><select aria-label="Memory allocation for new instances" value={settings.memory} onChange={(e) => setSettings((current) => ({ ...current, memory: Number(e.target.value) }))}>{[2, 3, 4, 6, 8, 12, 16].map((memory) => <option key={memory} value={memory}>{memory} GB</option>)}</select></div></div><div className="setting-row"><div><b>Java executable</b><small>{settings.javaPath || 'Automatic detection'}</small></div><div className="java-picker"><input aria-label="Java executable path" className="settings-input java-input" value={settings.javaPath} onChange={(e) => setSettings((current) => ({ ...current, javaPath: e.target.value }))} placeholder="Automatic" /><button className="button subtle" onClick={() => void chooseJava()} disabled={selectingJava}>{selectingJava ? 'Choosing…' : 'Browse for Java'}</button></div></div></SettingsGroup>
            </div><aside className="settings-aside"><div className="settings-note"><div className="tip-icon"><Icon name="info" size={16} /></div><h3>About these settings</h3><p>Preferences are saved locally. Memory and Java preferences are used when launching a game.</p></div><div className="version-card"><span>OBSIDIAN LAUNCHER</span><b>Early access</b><small>Version 0.1.0</small><div className="version-rule"/><small>Launcher preferences are stored on this device.</small></div></aside></div>
        </>}
        {status && <div className="status-toast" role="status" aria-live="polite"><span className="toast-dot" aria-hidden="true" />{status}<button onClick={() => setStatus('')} aria-label="Dismiss message"><Icon name="close" size={14} /></button></div>}
      </div>
    </main>

    {deviceCode && <div className="modal-backdrop" onClick={() => void cancelSignIn()}><section className="modal-card code-modal" role="dialog" aria-modal="true" aria-labelledby="microsoft-dialog-title" aria-describedby="microsoft-dialog-description" onClick={(e) => e.stopPropagation()}><button className="modal-close" onClick={() => void cancelSignIn()} aria-label="Cancel sign in"><Icon name="close" /></button><span className="modal-kicker">MICROSOFT SIGN IN</span><h2 id="microsoft-dialog-title">Finish signing in</h2><p id="microsoft-dialog-description">Enter this code on the Microsoft sign-in page to connect your account.</p><div className="device-code" aria-label={`Sign-in code ${deviceCode.userCode}`}>{deviceCode.userCode}</div><div className="code-footnote">Code expires in about {Math.ceil(deviceCode.expiresIn / 60)} minutes. Keep this window open.</div><button className="button secondary full-button" onClick={() => void window.core.openExternal(deviceCode.verificationUri)}><Icon name="external" size={16} /> Open sign-in page</button></section></div>}
    {showCreate && <div className="modal-backdrop" onClick={() => { setShowCreate(false); setEditingInstance(null); setNewName('') }}><form className="modal-card" role="dialog" aria-modal="true" aria-labelledby="instance-dialog-title" onSubmit={createInstance} onClick={(e) => e.stopPropagation()}>
      <button type="button" className="modal-close" onClick={() => { setShowCreate(false); setEditingInstance(null); setNewName('') }} aria-label="Close dialog"><Icon name="close" /></button>
      <span className="modal-kicker">{editingInstance ? 'EDIT INSTANCE' : 'NEW INSTANCE'}</span><h2 id="instance-dialog-title">{editingInstance ? 'Rename instance' : 'Create an instance'}</h2>
      <p>{editingInstance ? 'Choose a new name for this profile.' : 'Choose a name and version, then configure the profile in the next step.'}</p>
      <label className="field-label">INSTANCE NAME<input data-dialog-autofocus value={newName} onChange={(e) => setNewName(e.target.value)} maxLength={32} placeholder="e.g. Survival, Modded, Creative" required /></label>
      {!editingInstance && <>
        <label className="field-label">MINECRAFT VERSION
          <select value={newVersion} onChange={(e) => setNewVersion(e.target.value)} disabled={manifestLoading || !versionManifest?.versions.length} required>
            <option value="" disabled>{manifestLoading ? 'Loading Minecraft versions…' : versionManifest ? 'Choose a version' : 'Versions unavailable'}</option>
            {availableVersions.map((version) => <option key={version.id} value={version.id}>{version.id}{version.type === 'snapshot' ? ' · Snapshot' : ''}</option>)}
          </select>
        </label>
        <label className="snapshot-toggle"><input type="checkbox" checked={showSnapshots} onChange={(event) => {
          const enabled = event.target.checked
          setShowSnapshots(enabled)
          if (!enabled && versionManifest?.versions.find((version) => version.id === newVersion)?.type === 'snapshot') {
            const latestRelease = [...versionManifest.versions].filter((version) => version.type === 'release').sort((a, b) => versionTimestamp(b) - versionTimestamp(a))[0]
            if (latestRelease) setNewVersion(latestRelease.id)
          }
        }} /><span><b>Show snapshots</b><small>Include snapshot builds in the version list.</small></span></label>
        {manifestLoading && <div className="modal-hint" role="status">Fetching the latest Minecraft version manifest…</div>}
        {manifestError && <div className="manifest-error" role="alert"><span>{manifestError}</span><button type="button" className="text-button" onClick={() => void loadVersionManifest()}>Retry</button></div>}
        {versionManifest && !manifestLoading && <div className="modal-hint">{versionManifest.source === 'cache' ? 'Using the saved Mojang manifest because it could not be refreshed.' : 'Versions loaded from the Mojang manifest.'} {versionManifest.versions.length} versions available.</div>}
      </>}
      <div className="modal-actions"><button type="button" className="button subtle" onClick={() => { setShowCreate(false); setEditingInstance(null); setNewName('') }}>Cancel</button><button className="button primary" type="submit" disabled={!editingInstance && (!newVersion || manifestLoading || !versionManifest?.versions.length)}>{editingInstance ? 'Save name' : <><Icon name="arrow" size={16} /> Continue to setup</>}</button></div>
    </form></div>}
    {showInstanceSetup && configuredInstance && <InstanceSetupDialog instance={configuredInstance} versions={versionManifest?.versions ?? []} defaultMemory={settings.memory} defaultJavaPath={settings.javaPath} onUpdate={(changes) => updateInstance(configuredInstance.id, changes)} onClose={() => setShowInstanceSetup(false)} onContinue={() => startInstanceInstallation(configuredInstance)} />}
    {installingInstance && <InstallationDialog instanceName={installingInstance.name} version={installingInstance.version} progress={installationProgress} logs={installationLogs} status={installationStatus} onCancel={cancelInstanceInstallation} onClose={closeInstallationDialog} />}
    {deletingInstance && <div className="modal-backdrop" onClick={() => { if (!deletingBusy) setDeletingInstance(null) }}><section className="modal-card" role="dialog" aria-modal="true" aria-labelledby="delete-dialog-title" aria-describedby="delete-dialog-description" onClick={(e) => e.stopPropagation()}><button className="modal-close" onClick={() => setDeletingInstance(null)} disabled={deletingBusy} aria-label="Cancel delete"><Icon name="close" /></button><span className="modal-kicker">DELETE PROFILE</span><h2 id="delete-dialog-title">Delete “{deletingInstance.name}”?</h2><p id="delete-dialog-description">This removes this profile and its downloaded mods, resource packs, and shaders from this device.</p>{deleteError && <div className="manifest-error" role="alert">{deleteError}</div>}<div className="modal-actions"><button className="button subtle" onClick={() => setDeletingInstance(null)} disabled={deletingBusy}>Cancel</button><button className="button danger-confirm" onClick={() => void confirmDeleteInstance()} disabled={deletingBusy}>{deletingBusy ? 'Deleting…' : 'Delete profile and files'}</button></div></section></div>}
  </div>
}

function PageHeading({ eyebrow, title, description, action }: { eyebrow: string; title: string; description: string; action?: React.ReactNode }) {
  return <div className="page-heading"><div><div className="eyebrow"><span className="eyebrow-line" />{eyebrow}</div><h1>{title}</h1><p>{description}</p></div>{action}</div>
}
function LoginScreen({ saved, offlineName, setOfflineName, status, authStarting, signInMicrosoft, addOffline, onSelectAccount, deviceCode, cancelSignIn }: {
  saved: SavedAccount[]
  offlineName: string
  setOfflineName: (name: string) => void
  status: string
  authStarting: boolean
  signInMicrosoft: () => void
  addOffline: () => void
  onSelectAccount: (account: SavedAccount) => void
  deviceCode: DeviceCode | null
  cancelSignIn: () => void
}) {
  return <main className="auth-screen" aria-label="Sign in to Obsidian Launcher">
    <div className="auth-atmosphere" aria-hidden="true"><span/><span/><span/></div>
    <div className="auth-window">
      <header className="auth-topline"><div className="auth-brand"><span className="auth-logo"><img src={logo} alt="" /></span><span><b>OBSIDIAN</b><small>LAUNCHER</small></span></div><span className="auth-version">EARLY ACCESS · 0.1.0</span></header>
      <div className="auth-layout">
        <section className="auth-intro"><span className="auth-kicker"><i/> MINECRAFT LAUNCHER</span><h1>Sign in to<br/><em>continue.</em></h1><p>Choose an account to open your launcher library. Your saved profiles stay on this device.</p><div className="auth-feature"><span><Icon name="cube" size={17}/></span><div><b>One home for your instances</b><small>Keep versions and setups organized.</small></div></div></section>
        <section className="auth-panel">
          <div className="auth-panel-heading"><h2>Welcome back</h2><p>Sign in with Microsoft or use a local profile.</p></div>
          <button className="auth-microsoft-button" onClick={signInMicrosoft} aria-disabled={authStarting} aria-busy={authStarting}><span className="ms-mark" aria-hidden="true"><i/><i/><i/><i/></span><span>{authStarting ? 'Connecting to Microsoft…' : 'Continue with Microsoft'}</span><Icon name="arrow" size={16}/></button>
          <div className="auth-divider" aria-hidden="true"><span/>OR USE AN OFFLINE PROFILE<span/></div>
          <form className="auth-offline-form" onSubmit={(event) => { event.preventDefault(); addOffline() }}><label htmlFor="offline-name">USERNAME</label><div className="auth-input-row"><input id="offline-name" aria-describedby="offline-name-help" autoComplete="username" value={offlineName} onChange={(event) => setOfflineName(event.target.value)} maxLength={16} minLength={3} pattern="[A-Za-z0-9_]{3,16}" placeholder="Your Minecraft name" required/><button type="submit" aria-label="Continue with offline profile"><Icon name="arrow" size={17}/></button></div><small id="offline-name-help">3–16 characters: letters, numbers, and underscores.</small></form>
          {saved.length > 0 && <div className="auth-saved"><div className="auth-saved-title">SAVED ON THIS DEVICE</div>{saved.map((account) => <button className="auth-saved-row" key={account.id} aria-label={`Sign in as ${account.username}, ${account.type === 'offline' ? 'offline profile' : 'Microsoft account'}`} onClick={() => onSelectAccount(account)}><span className="auth-saved-avatar" aria-hidden="true">{account.username.slice(0, 1).toUpperCase()}</span><span><b>{account.username}</b><small>{account.type === 'offline' ? 'Offline profile' : 'Microsoft account'}</small></span><Icon name="arrow" size={14}/></button>)}</div>}
          {status && <div className="auth-status" role="status" aria-live="polite"><span aria-hidden="true"/>{status}</div>}
          <div className="auth-footnote">Microsoft sign-in opens in your browser. Your password is never shared with Obsidian.</div>
        </section>
      </div>
      <footer className="auth-footer"><span>OBSIDIAN LAUNCHER</span><span>ACCOUNT REQUIRED TO ENTER</span></footer>
    </div>
    {deviceCode && <div className="modal-backdrop" onClick={cancelSignIn}><section className="modal-card code-modal" role="dialog" aria-modal="true" aria-labelledby="login-microsoft-dialog-title" aria-describedby="login-microsoft-dialog-description" onClick={(event) => event.stopPropagation()}><button className="modal-close" onClick={cancelSignIn} aria-label="Cancel sign in"><Icon name="close" /></button><span className="modal-kicker">MICROSOFT SIGN IN</span><h2 id="login-microsoft-dialog-title">Finish signing in</h2><p id="login-microsoft-dialog-description">Enter this code on the Microsoft sign-in page to connect your account.</p><div className="device-code" aria-label={`Sign-in code ${deviceCode.userCode}`}>{deviceCode.userCode}</div><div className="code-footnote">Code expires in about {Math.ceil(deviceCode.expiresIn / 60)} minutes. Keep this window open.</div><button className="button secondary full-button" onClick={() => void window.core.openExternal(deviceCode.verificationUri)}><Icon name="external" size={16} /> Open sign-in page</button></section></div>}
  </main>
}
function SettingsGroup({ title, description, children }: { title: string; description: string; children: React.ReactNode }) {
  return <section className="settings-group"><div className="settings-group-head"><h3>{title}</h3><p>{description}</p></div><div className="settings-group-body">{children}</div></section>
}
