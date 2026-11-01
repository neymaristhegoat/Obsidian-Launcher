import { spawn, ChildProcessWithoutNullStreams } from 'node:child_process'
import { EventEmitter } from 'node:events'
import readline from 'node:readline'

export class CoreClient extends EventEmitter {
  private proc: ChildProcessWithoutNullStreams
  private nextId = 1
  private pending = new Map<number, { resolve: (v: any) => void; reject: (e: Error) => void }>()

  constructor(exePath: string, args: string[] = []) {
    super()
    this.proc = spawn(exePath, [], { stdio: ['pipe', 'pipe', 'pipe'] })
    this.proc.on('error', (err) => console.error('[core] failed to start:', err.message))
    readline.createInterface({ input: this.proc.stdout }).on('line', (line) => this.onLine(line))
    this.proc.stderr.on('data', (d) => console.error('[core]', d.toString().trimEnd()))
    this.proc.on('exit', (code) => {
      for (const p of this.pending.values()) p.reject(new Error(`core exited (${code})`))
      this.pending.clear()
    })
  }

  private onLine(line: string) {
    let msg: any
    try { msg = JSON.parse(line) } catch { return }
    if (msg.id !== undefined) {
      const p = this.pending.get(msg.id)
      if (!p) return
      this.pending.delete(msg.id)
      msg.error ? p.reject(new Error(msg.error.message)) : p.resolve(msg.result)
    } else if (msg.method) {
      this.emit('notification', msg.method, msg.params)
    }
  }

  call(method: string, params: unknown = {}): Promise<any> {
    const id = this.nextId++
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject })
      this.proc.stdin.write(JSON.stringify({ jsonrpc: '2.0', id, method, params }) + '\n')
    })
  }

  kill() { this.proc.kill() }
}