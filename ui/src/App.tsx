import { useEffect, useState } from 'react'

const CLIENT_ID = '6be102b8-2980-4c03-bd3b-3185c27713f4'

export default function App() {
  const [code, setCode] = useState<{ userCode: string; verificationUri: string } | null>(null)
  const [status, setStatus] = useState('Not signed in')

  useEffect(() => {
    const off = window.core.onEvent((e: any) => {
      if (e.method === 'auth.complete') { setCode(null); setStatus(`Signed in as ${e.params.username}`) }
      if (e.method === 'auth.failed')   { setCode(null); setStatus(`Failed: ${e.params.message}`) }
    })
    // try a silent login with the saved refresh token
    window.core.call('auth.refresh', { clientId: CLIENT_ID })
      .then((p: any) => setStatus(`Signed in as ${p.username}`))
      .catch(() => {})
    return off
  }, [])

  async function signIn() {
  setStatus('Requesting sign-in code...')
  try {
    const r = await window.core.call('auth.start', { clientId: CLIENT_ID })
    setCode(r)
    setStatus('Waiting for you to sign in...')
    if (window.core.openExternal) await window.core.openExternal(r.verificationUri)
    else setStatus('openExternal missing from preload, open the link manually')
  } catch (e: any) {
    setStatus(`Error: ${e.message}`)
  }
}

  return (
    <div style={{ padding: 24, fontFamily: 'sans-serif' }}>
      <button onClick={signIn}>Sign in with Microsoft</button>
      <button onClick={() => window.core.call('auth.logout').then(() => setStatus('Not signed in'))}
              style={{ marginLeft: 8 }}>Sign out</button>
      {code && <h2>Enter code: {code.userCode}</h2>}
      <p>{status}</p>
    </div>
  )
}