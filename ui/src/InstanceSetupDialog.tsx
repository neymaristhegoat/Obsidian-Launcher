import { useEffect, useMemo, useState } from 'react'

type Loader = 'vanilla' | 'fabric' | 'forge' | 'neoforge' | 'quilt'
type ContentType = 'mod' | 'resourcepack' | 'shader'
type WorkspaceTab = 'settings' | ContentType

interface InstanceModel {
  id: string
  name: string
  version: string
  createdAt: number
  loader?: Loader
  memory?: number
  javaPath?: string
}
interface VersionOption { id: string; type: string; releaseTime: string; time: string }
interface ModrinthProject {
  project_id: string
  project_type: string
  slug: string | null
  title: string
  description: string
  author: string
  downloads: number
  icon_url: string | null
  versions: string[]
}
interface InstalledProject {
  projectId: string
  projectType: ContentType
  title: string
  author: string
  description: string
  iconUrl: string
  filename: string
  versionNumber: string
  installedAt: number
}
const versionTimestamp = (version: VersionOption) => Date.parse(version.releaseTime || version.time) || 0
const typeNames: Record<ContentType, string> = { mod: 'Mods', resourcepack: 'Texture packs', shader: 'Shaders' }

function WorkspaceIcon({ name, size = 18 }: { name: string; size?: number }) {
  const paths: Record<string, string[]> = {
    settings: ['M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z', 'M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06-1.5 2.6-.08-.03a1.65 1.65 0 0 0-1.7.18 1.65 1.65 0 0 0-.81 1.42V21h-3v-.1a1.65 1.65 0 0 0-1.1-1.56 1.65 1.65 0 0 0-1.8.33l-.08.06-2.6-1.5.03-.08a1.65 1.65 0 0 0-.18-1.7 1.65 1.65 0 0 0-1.42-.81H5v-3h.1a1.65 1.65 0 0 0 1.56-1.1 1.65 1.65 0 0 0-.33-1.8l-.06-.08 1.5-2.6.08.03a1.65 1.65 0 0 0 1.7-.18A1.65 1.65 0 0 0 10.36 5.5V5h3v.1a1.65 1.65 0 0 0 1.1 1.56 1.65 1.65 0 0 0 1.8-.33l.08-.06 2.6 1.5-.03.08a1.65 1.65 0 0 0 .18 1.7 1.65 1.65 0 0 0 1.42.81H21v3h-.1a1.65 1.65 0 0 0-1.5 1.64Z'],
    puzzle: ['M19 13h-2a2 2 0 1 1-4 0V5h-3a2 2 0 1 0-4 0v3H4a2 2 0 1 0 0 4h2v3a2 2 0 1 0 4 4h3v-2a2 2 0 1 1 4 0v2h2a2 2 0 0 0 2-2v-2a2 2 0 0 0-2-2Z'],
    image: ['M4 4h16v16H4z', 'm4 15 4-4 4 4 4-6 4 6', 'M9 9h.01'],
    sun: ['M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8Z', 'M12 2v2', 'M12 20v2', 'm4.93 4.93 1.42 1.42', 'm17.65 17.65 1.42 1.42', 'M2 12h2', 'M20 12h2', 'm6.34 17.66-1.41 1.41', 'm19.07 4.93-1.42 1.42'],
    search: ['M11 19a8 8 0 1 1 0-16 8 8 0 0 1 0 16Z', 'm21 21-4.35-4.35'],
    arrow: ['M5 12h14', 'm12 5 7 7-7 7'],
    external: ['M15 3h6v6', 'm10 14 11-11', 'M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6'],
    plus: ['M12 5v14', 'M5 12h14'],
    check: ['m5 12 4 4L19 6'],
    close: ['m18 6-12 12', 'm6 6 12 12'],
    info: ['M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20Z', 'M12 16v-4', 'M12 8h.01'],
  }
  return <svg width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.8" strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">{(paths[name] ?? paths.info).map((d, index) => <path key={`${name}-${index}`} d={d}/>)}</svg>
}

export default function InstanceSetupDialog({ instance, versions, defaultMemory, defaultJavaPath, onUpdate, onClose, onContinue }: {
  instance: InstanceModel
  versions: VersionOption[]
  defaultMemory: number
  defaultJavaPath: string
  onUpdate: (changes: Partial<InstanceModel>) => void
  onClose: () => void
  onContinue: () => void
}) {
  const [tab, setTab] = useState<WorkspaceTab>('settings')
  const availableVersions = useMemo(() => {
    const releases = versions.filter((version) => version.type === 'release' || version.type === 'snapshot')
    if (!releases.some((version) => version.id === instance.version)) releases.push({ id: instance.version, type: 'release', releaseTime: '', time: '' })
    return releases.sort((a, b) => versionTimestamp(b) - versionTimestamp(a))
  }, [instance.version, versions])

  const sections: { id: WorkspaceTab; label: string; icon: string }[] = [
    { id: 'settings', label: 'Settings', icon: 'settings' },
    { id: 'mod', label: 'Mods', icon: 'puzzle' },
    { id: 'shader', label: 'Shaders', icon: 'sun' },
    { id: 'resourcepack', label: 'Texture packs', icon: 'image' },
  ]

  return <div className="instance-setup-backdrop" onMouseDown={(event) => { if (event.target === event.currentTarget) onClose() }}>
    <section className="instance-setup-dialog" role="dialog" aria-modal="true" aria-labelledby="instance-workspace-title" aria-describedby="instance-workspace-description" onMouseDown={(event) => event.stopPropagation()}>
      <aside className="instance-setup-sidebar">
        <div className="instance-setup-brand"><span><WorkspaceIcon name="puzzle" size={20}/></span><div><b>INSTANCE</b><small>SETUP</small></div></div>
        <span className="instance-setup-label">CONFIGURE</span>
        <nav className="instance-setup-nav" aria-label="Instance setup sections">
          {sections.map((section) => <button key={section.id} type="button" aria-current={tab === section.id ? 'page' : undefined} className={tab === section.id ? 'active' : ''} data-dialog-autofocus={tab === section.id ? true : undefined} onClick={() => setTab(section.id)}><WorkspaceIcon name={section.icon} size={17}/><span>{section.label}</span></button>)}
        </nav>
        <div className="instance-setup-profile"><span className="instance-setup-profile-icon"><WorkspaceIcon name="cube" size={17}/></span><span><b>{instance.name}</b><small>Minecraft {instance.version}</small></span></div>
      </aside>

      <div className="instance-setup-main">
        <header className="instance-setup-heading">
          <div><span className="eyebrow"><span className="eyebrow-line"/>INSTANCE CONFIGURATION</span><h1 id="instance-workspace-title">{instance.name}</h1><p id="instance-workspace-description">Minecraft {instance.version} <span>·</span> Configure this profile and its content.</p></div>
          <button className="modal-close instance-setup-close" type="button" onClick={onClose} aria-label="Close instance setup"><WorkspaceIcon name="close" size={18}/></button>
        </header>
        <div className="instance-setup-content">
          {tab === 'settings' ? <InstanceSettings instance={instance} versions={availableVersions} defaultMemory={defaultMemory} defaultJavaPath={defaultJavaPath} onUpdate={onUpdate} />
            : <ModrinthTab key={tab} instance={instance} type={tab} onChooseLoader={() => setTab('settings')} />}
        </div>
        <footer className="instance-setup-footer"><span><WorkspaceIcon name="check" size={14}/> Changes save automatically</span><button className="button primary" type="button" onClick={onContinue}>Continue <WorkspaceIcon name="arrow" size={16}/></button></footer>
      </div>
    </section>
  </div>
}

function InstanceSettings({ instance, versions, defaultMemory, defaultJavaPath, onUpdate }: {
  instance: InstanceModel
  versions: VersionOption[]
  defaultMemory: number
  defaultJavaPath: string
  onUpdate: (changes: Partial<InstanceModel>) => void
}) {
  const [selectingJava, setSelectingJava] = useState(false)
  const [error, setError] = useState('')
  async function browseJava() {
    setSelectingJava(true)
    setError('')
    try {
      const path = await window.core.pickFile()
      if (path) onUpdate({ javaPath: path })
    } catch (e) { setError(String((e as Error)?.message ?? e)) }
    finally { setSelectingJava(false) }
  }

  return <div className="workspace-settings-card workspace-configuration-card">
    <div className="workspace-section-title"><div><h2>Instance settings</h2><p>Choose how this profile is set up.</p></div><span className="autosave-pill"><WorkspaceIcon name="check" size={13}/> Autosaved</span></div>
    <div className="workspace-config-fields">
      <label className="workspace-field">INSTANCE NAME<input value={instance.name} maxLength={32} onChange={(event) => onUpdate({ name: event.target.value })}/></label>
      <label className="workspace-field">MINECRAFT VERSION<select value={instance.version} onChange={(event) => onUpdate({ version: event.target.value })}>{versions.map((version) => <option key={version.id} value={version.id}>{version.id}{version.type === 'snapshot' ? ' · Snapshot' : ''}</option>)}</select></label>
      <label className="workspace-field">MOD LOADER<select value={instance.loader ?? 'vanilla'} onChange={(event) => onUpdate({ loader: event.target.value as Loader })}><option value="vanilla">Vanilla</option><option value="fabric">Fabric</option><option value="forge">Forge</option><option value="neoforge">NeoForge</option><option value="quilt">Quilt</option></select><small>This installer downloads vanilla game files. Mod loader installation is separate and is not available yet.</small></label>
      <label className="workspace-field">MEMORY ALLOCATION<select value={instance.memory ?? defaultMemory} onChange={(event) => onUpdate({ memory: Number(event.target.value) })}>{[2, 3, 4, 6, 8, 12, 16].map((memory) => <option key={memory} value={memory}>{memory} GB</option>)}</select></label>
      <div className="workspace-java-field"><label className="workspace-field">JAVA EXECUTABLE<input value={instance.javaPath ?? defaultJavaPath} onChange={(event) => onUpdate({ javaPath: event.target.value })} placeholder="Automatic detection"/></label><button className="button subtle compact" onClick={() => void browseJava()} disabled={selectingJava}>{selectingJava ? 'Choosing…' : 'Browse'}</button></div>
    </div>
    <div className="workspace-java-actions"><span>{instance.javaPath || defaultJavaPath ? 'Using the selected Java executable.' : 'Java will be detected automatically when you launch this profile.'}</span></div>
    {error && <div className="manifest-error" role="alert">{error}</div>}
  </div>
}

function ModrinthTab({ instance, type, onChooseLoader }: { instance: InstanceModel; type: ContentType; onChooseLoader: () => void }) {
  const [query, setQuery] = useState('')
  const [hits, setHits] = useState<ModrinthProject[]>([])
  const [totalHits, setTotalHits] = useState(0)
  const [installed, setInstalled] = useState<InstalledProject[]>([])
  const [loading, setLoading] = useState(false)
  const [loadingInstalled, setLoadingInstalled] = useState(true)
  const [activeProject, setActiveProject] = useState('')
  const [error, setError] = useState('')
  const [notice, setNotice] = useState('')

  async function refreshInstalled() {
    setLoadingInstalled(true)
    try {
      const result = await window.core.call<{ items: InstalledProject[] }>('modrinth.installed', { instanceId: instance.id })
      setInstalled(Array.isArray(result.items) ? result.items : [])
    } catch (e) { setError(String((e as Error)?.message ?? e)) }
    finally { setLoadingInstalled(false) }
  }
  useEffect(() => { void refreshInstalled() }, [instance.id])

  async function searchProjects(event?: React.FormEvent<HTMLFormElement>) {
    event?.preventDefault()
    setLoading(true)
    setError('')
    setNotice('')
    try {
      const result = await window.core.call<{ hits: ModrinthProject[]; totalHits: number }>('modrinth.search', {
        projectType: type,
        minecraftVersion: instance.version,
        loader: instance.loader ?? 'vanilla',
        query: query.trim(),
      })
      setHits(result.hits)
      setTotalHits(result.totalHits)
    } catch (e) { setError(String((e as Error)?.message ?? e)); setHits([]); setTotalHits(0) }
    finally { setLoading(false) }
  }

  async function installProject(project: ModrinthProject) {
    setActiveProject(project.project_id)
    setError('')
    setNotice('')
    try {
      await window.core.call<InstalledProject>('modrinth.install', {
        instanceId: instance.id,
        projectType: type,
        projectId: project.project_id,
        minecraftVersion: instance.version,
        loader: instance.loader ?? 'vanilla',
      })
      setNotice(`${project.title} was added to ${typeNames[type].toLowerCase()}.`)
      await refreshInstalled()
    } catch (e) { setError(String((e as Error)?.message ?? e)) }
    finally { setActiveProject('') }
  }

  async function removeProject(project: InstalledProject) {
    setActiveProject(project.projectId)
    setError('')
    setNotice('')
    try {
      await window.core.call('modrinth.remove', { instanceId: instance.id, projectType: type, projectId: project.projectId })
      setInstalled((current) => current.filter((item) => item.projectId !== project.projectId || item.projectType !== type))
      setNotice(`${project.title} was removed from this profile.`)
    } catch (e) { setError(String((e as Error)?.message ?? e)) }
    finally { setActiveProject('') }
  }

  const currentInstalled = installed.filter((item) => item.projectType === type)
  const installedIds = new Set(currentInstalled.map((item) => item.projectId))
  const loaderMissing = type === 'mod' && (!instance.loader || instance.loader === 'vanilla')

  return <div className="modrinth-layout">
    <section className="modrinth-browser">
      <header className="modrinth-heading"><div><div className="eyebrow"><span className="eyebrow-line"/>MODRINTH LIBRARY</div><h2>Browse {typeNames[type].toLowerCase()}</h2><p>Compatible with Minecraft {instance.version}{type === 'mod' ? ` · ${instance.loader ?? 'vanilla'}` : ''}</p></div><a className="modrinth-badge" href="https://modrinth.com" onClick={(event) => { event.preventDefault(); void window.core.openExternal('https://modrinth.com') }}>MODRINTH <WorkspaceIcon name="external" size={13}/></a></header>
      <form className="modrinth-search" onSubmit={(event) => void searchProjects(event)}><WorkspaceIcon name="search" size={17}/><input aria-label={`Search Modrinth ${typeNames[type].toLowerCase()}`} value={query} onChange={(event) => setQuery(event.target.value)} placeholder={`Search ${typeNames[type].toLowerCase()}…`}/><button className="button primary" type="submit" disabled={loading}>{loading ? 'Searching…' : 'Search'}</button></form>
      {loaderMissing && <div className="workspace-info-banner"><WorkspaceIcon name="info" size={17}/><span>Choose a mod loader before browsing mods.</span><button className="text-button" onClick={onChooseLoader}>Configure loader <WorkspaceIcon name="arrow" size={14}/></button></div>}
      {error && <div className="manifest-error" role="alert">{error}</div>}
      {notice && <div className="workspace-success" role="status"><WorkspaceIcon name="check" size={15}/>{notice}</div>}
      <div className="modrinth-results-heading"><b>{loading ? 'Searching Modrinth…' : hits.length ? `${hits.length} of ${totalHits.toLocaleString()} projects` : 'Search Modrinth'}</b><span>Minecraft {instance.version}</span></div>
      {hits.length ? <div className="modrinth-results">{hits.map((project) => <article className="modrinth-project" key={project.project_id}>
        <div className="modrinth-project-icon">{project.icon_url ? <img src={project.icon_url} alt="" loading="lazy"/> : <WorkspaceIcon name={type === 'mod' ? 'puzzle' : type === 'shader' ? 'sun' : 'image'} size={20}/>}</div>
        <div className="modrinth-project-copy"><h3>{project.title}</h3><small>by {project.author} <span>·</span> {project.downloads.toLocaleString()} downloads</small><p>{project.description}</p><div className="modrinth-version-tags">{project.versions.slice(0, 5).map((version) => <span key={version}>{version}</span>)}</div></div>
        <div className="modrinth-project-actions">{project.slug && <button className="project-external" aria-label={`Open ${project.title} on Modrinth`} title="Open on Modrinth" onClick={() => void window.core.openExternal(`https://modrinth.com/${type}/${project.slug}`)}><WorkspaceIcon name="external" size={15}/></button>}{installedIds.has(project.project_id) ? <span className="installed-project-pill"><WorkspaceIcon name="check" size={13}/> Added</span> : <button className="button subtle compact" disabled={Boolean(activeProject) || (loaderMissing && type === 'mod')} onClick={() => void installProject(project)}>{activeProject === project.project_id ? 'Adding…' : <><WorkspaceIcon name="plus" size={14}/> Add</>}</button>}</div>
      </article>)}</div> : <div className="modrinth-empty"><span className="empty-orb"><WorkspaceIcon name={type === 'mod' ? 'puzzle' : type === 'shader' ? 'sun' : 'image'} size={24}/></span><b>{loading ? 'Contacting Modrinth' : 'Find your next addition'}</b><p>{loading ? 'Loading projects compatible with this profile.' : `Search Modrinth for ${typeNames[type].toLowerCase()} compatible with Minecraft ${instance.version}.`}</p></div>}
    </section>
    <aside className="installed-content-card"><div className="installed-content-heading"><div><span className="eyebrow"><span className="eyebrow-line"/>THIS INSTANCE</span><h2>Installed {typeNames[type].toLowerCase()}</h2></div><span className="installed-count">{currentInstalled.length}</span></div>
      <p>Files stored in <code>{type === 'mod' ? 'mods' : type === 'shader' ? 'shaderpacks' : 'resourcepacks'}/</code> for {instance.name}.</p>
      {loadingInstalled ? <div className="installed-empty">Loading this profile…</div> : currentInstalled.length ? <div className="installed-list">{currentInstalled.map((project) => <div className="installed-item" key={project.projectId}><span className="modrinth-project-icon small">{project.iconUrl ? <img src={project.iconUrl} alt="" loading="lazy"/> : <WorkspaceIcon name="check" size={16}/>}</span><span className="installed-item-copy"><b>{project.title}</b><small>{project.versionNumber || project.filename}</small></span><button className="remove-button" aria-label={`Remove ${project.title}`} onClick={() => void removeProject(project)} disabled={Boolean(activeProject)}><WorkspaceIcon name="close" size={15}/></button></div>)}</div> : <div className="installed-empty">Nothing installed here yet. Search Modrinth to add compatible content.</div>}
      <div className="installed-footnote"><WorkspaceIcon name="info" size={15}/><span>Modrinth installs files into this profile. Game and loader installation are separate steps.</span></div>
    </aside>
  </div>
}
