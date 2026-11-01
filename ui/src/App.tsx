import { useEffect, useRef, useState } from 'react'
import logo from './assets/logo.svg'

const CLIENT_ID = 'PASTE-YOUR-AZURE-CLIENT-ID'

interface SavedAccount {
  id: string
  type: 'offline' | 'microsoft'
  username: string
  uuid: string
}
interface Session extends SavedAccount {
  accessToken: string
  offline: boolean
}

const cleanError = (e: any) =>
  String(e?.message ?? e).replace(/^Error invoking remote method '.*?': (Error: )?/, '')

export default function App() {
  const [saved, setSaved] = useState<SavedAccount[]>([])
  const [session, setSession] = useState<Session | null>(null)
  const [code, setCode] = useState<{ userCode: string; verificationUri: string } | null>(null)
  const [status, setStatus] = useState('')
  const [offlineName, setOfflineName] = useState('')
  const started = useRef(false)

  async function loadSaved() {
    const r = await window.core.call<{ active: string; accounts: SavedAccount[] }>('accounts.list')
    setSaved(r.accounts)
    return r
  }

  async function useAccount(a: SavedAccount) {
    setStatus(`Signing in as ${a.username}...`)
    try {
      const s = await window.core.call<Session>('accounts.select', { id: a.id, clientId: CLIENT_ID })
      setSession(s)
      setStatus('')
    } catch (e) {
      setSession(null)
      setStatus(a.type === 'microsoft'
        ? `Couldn't restore ${a.username}: ${cleanError(e)}`
        : cleanError(e))
    }
  }

  // events from the core (Microsoft device-code flow)
  useEffect(() => {
    return window.core.onEvent((e: any) => {
      if (e.method === 'auth.complete') {
        setCode(null); setSession(e.params); setStatus(''); loadSaved()
      }
      if (e.method === 'auth.failed') {
        setCode(null); setStatus(`Sign-in failed: ${e.params.message}`)
      }
    })
  }, [])

  // restore the last used account once on startup
  // (guarded, because React dev mode runs effects twice and refresh tokens rotate)
  useEffect(() => {
    if (started.current) return
    started.current = true
    ;(async () => {
      const r = await loadSaved()
      const active = r.accounts.find((a) => a.id === r.active)
      if (active) await useAccount(active)
    })()
  }, [])

  async function signInMicrosoft() {
    setStatus('Requesting sign-in code...')
    try {
      const r = await window.core.call('auth.start', { clientId: CLIENT_ID })
      setCode(r)
      setStatus('Waiting for you to sign in...')
      window.core.openExternal?.(r.verificationUri)
    } catch (e) {
      setStatus(`Error: ${cleanError(e)}`)
    }
  }

  async function addOffline() {
    try {
      const s = await window.core.call<Session>('auth.offline', { username: offlineName.trim() })
      setSession(s)
      setOfflineName('')
      setStatus('')
      loadSaved()
    } catch (e) {
      setStatus(cleanError(e))
    }
  }

  async function signOut() {
    await window.core.call('accounts.signout')
    setSession(null)
    setStatus('')
  }

  async function forget(a: SavedAccount) {
    await window.core.call('accounts.remove', { id: a.id })
    if (session?.id === a.id) setSession(null)
    loadSaved()
  }

  const card: React.CSSProperties = {
    display: 'flex', alignItems: 'center', gap: 8, padding: '8px 10px',
    background: '#150d24', border: '1px solid #2a1b45', borderRadius: 6, marginBottom: 6,
  }

  return (
    <div style={{ minHeight: '100vh', display: 'grid', placeItems: 'center',
                  background: '#0b0614', color: '#e9e3f7', fontFamily: 'system-ui, sans-serif' }}>
      <div style={{ width: 380, textAlign: 'center', padding: '24px 0' }}>
        <img src={logo} alt="" width={140} height={140} style={{ display: 'block', margin: '0 auto' }} />
        <h1 style={{ margin: '4px 0 24px', fontSize: 28, fontWeight: 600, lineHeight: 1.2, whiteSpace: 'nowrap' }}>
          Obsidian Launcher
        </h1>

        {session && (
          <div style={{ marginBottom: 16 }}>
            <p style={{ margin: '0 0 8px' }}>
              Signed in as <b>{session.username}</b>{session.offline && ' (offline)'}
            </p>
            <button onClick={signOut}>Sign out</button>
          </div>
        )}

        {saved.length > 0 && (
          <div style={{ textAlign: 'left', marginBottom: 16 }}>
            <div style={{ opacity: 0.6, fontSize: 13, marginBottom: 6 }}>Saved accounts</div>
            {saved.map((a) => (
              <div key={a.id} style={card}>
                <span style={{ flex: 1 }}>
                  {a.username}
                  <span style={{ opacity: 0.5, fontSize: 12 }}>
                    {' '}{a.type === 'offline' ? '· offline' : '· Microsoft'}
                  </span>
                </span>
                {session?.id === a.id
                  ? <span style={{ color: '#a78bfa', fontSize: 12 }}>active</span>
                  : <button onClick={() => useAccount(a)}>Use</button>}
                <button onClick={() => forget(a)} title="Forget this account">✕</button>
              </div>
            ))}
          </div>
        )}

        <button onClick={signInMicrosoft} style={{ width: '100%', padding: 10 }}>
          Sign in with Microsoft
        </button>
        {code && <h2 style={{ letterSpacing: 2 }}>{code.userCode}</h2>}

        <p style={{ opacity: 0.6, margin: '18px 0 8px' }}>or add an offline account</p>
        <div style={{ display: 'flex', gap: 8 }}>
          <input
            value={offlineName}
            onChange={(e) => setOfflineName(e.target.value)}
            onKeyDown={(e) => e.key === 'Enter' && addOffline()}
            placeholder="Username"
            maxLength={16}
            style={{ flex: 1, padding: 8 }}
          />
          <button onClick={addOffline}>Add offline</button>
        </div>

        {status && <p style={{ color: '#c4b5fd', marginTop: 16 }}>{status}</p>}
      </div>
    </div>
  )
}