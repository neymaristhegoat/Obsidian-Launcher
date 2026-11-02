import { contextBridge, ipcRenderer } from 'electron'

contextBridge.exposeInMainWorld('core', {
  openExternal: (url:string) => ipcRenderer.invoke('shell:open', url),
  pickFile: () => ipcRenderer.invoke('dialog:pick-file'),
  launchGame: (request: { instanceId: string; session: unknown }) => ipcRenderer.invoke('game:launch', request),
  call: (method: string, params?: unknown) => ipcRenderer.invoke('core:call', method, params),
  onEvent: (cb: (e: { method: string; params: unknown }) => void) => {
    const handler = (_: unknown, e: any) => cb(e)
    ipcRenderer.on('core:event', handler)
    return () => ipcRenderer.removeListener('core:event', handler)
  },
})
