import { useEffect, useState } from 'react'

export default function App() {
  const [reply, setReply] = useState('')
  const [events, setEvents] = useState<string[]>([])

  useEffect(() => {
    return window.core.onEvent((e) =>
      setEvents((prev) => [...prev, `${e.method} ${JSON.stringify(e.params)}`])
    )
  }, [])

  return (
    <div style={{ padding: 24, fontFamily: 'sans-serif' }}>
      <button onClick={async () => setReply(JSON.stringify(await window.core.call('core.ping')))}>
        Ping core
      </button>
      <button onClick={() => window.core.call('core.slowTest')} style={{ marginLeft: 8 }}>
        Slow test
      </button>
      <p>Reply: {reply}</p>
      <pre>{events.join('\n')}</pre>
    </div>
  )
}