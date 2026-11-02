import { useEffect, useRef } from 'react'

export type InstallationStatus = 'installing' | 'cancelling' | 'complete' | 'failed' | 'cancelled'

export default function InstallationDialog({
  instanceName,
  version,
  progress,
  logs,
  status,
  onCancel,
  onClose,
}: {
  instanceName: string
  version: string
  progress: number
  logs: string[]
  status: InstallationStatus
  onCancel: () => void
  onClose: () => void
}) {
  const logRef = useRef<HTMLDivElement>(null)
  const active = status === 'installing' || status === 'cancelling'
  const label = status === 'complete' ? 'Complete' : status === 'failed' ? 'Failed' : status === 'cancelled' ? 'Cancelled' : status === 'cancelling' ? 'Stopping' : 'In progress'

  useEffect(() => {
    if (logRef.current) logRef.current.scrollTop = logRef.current.scrollHeight
  }, [logs])

  return <div className="installing-backdrop">
    <section className="installing-dialog" role="dialog" aria-modal="true" aria-labelledby="installing-title" aria-describedby="installing-description">
      <header className="installing-heading">
        <span className={`installing-mark ${active ? 'active' : status}`} aria-hidden="true">{active ? <i/> : status === 'complete' ? '✓' : status === 'failed' ? '!' : 'Ⅱ'}</span>
        <div className="installing-heading-copy"><span className="eyebrow"><span className="eyebrow-line"/>INSTANCE SETUP</span><h1 id="installing-title">Installing</h1><p id="installing-description">Minecraft {version} · {instanceName}</p></div>
        <span className={`installing-state ${status}`}>{label}</span>
      </header>

      <section className="installing-progress-section" aria-label="Installation progress">
        <div className="installing-progress-label"><span>Installing Minecraft {version}</span><b>{active ? `${progress}%` : status === 'complete' ? '100%' : ''}</b></div>
        <div className="installing-progress-track" role="progressbar" aria-label={`Installing Minecraft ${version}`} aria-valuemin={0} aria-valuemax={100} aria-valuenow={progress} aria-valuetext={active ? `${progress}% complete` : label}><span style={{ width: `${progress}%` }}/></div>
      </section>

      <section className="installing-log-section" aria-label="Installation log">
        <div className="installing-log-heading"><b>Install log</b><span>{logs.length} {logs.length === 1 ? 'entry' : 'entries'}</span></div>
        <div className="installing-log" role="log" aria-live="polite" aria-relevant="additions text" ref={logRef}>
          {logs.map((line, index) => <div className="installing-log-line" key={`${index}-${line}`}><span aria-hidden="true">›</span><code>{line}</code></div>)}
          {active && <div className="installing-log-line pending"><span aria-hidden="true">›</span><code>{status === 'cancelling' ? 'Waiting for the active download to stop…' : 'Waiting for the next installation step…'}</code></div>}
        </div>
      </section>

      <footer className="installing-footer">
        <span>{status === 'complete' ? 'The game files are ready in this instance.' : status === 'failed' ? 'The installer stopped after an error.' : status === 'cancelled' ? 'The installation was stopped.' : 'Keep this window open while files are being prepared.'}</span>
        {active ? <button className="button subtle" type="button" onClick={onCancel} disabled={status === 'cancelling'}>{status === 'cancelling' ? 'Stopping…' : 'Cancel installation'}</button> : <button className="button primary" type="button" onClick={onClose}>{status === 'complete' ? 'Done' : 'Close'}</button>}
      </footer>
    </section>
  </div>
}
