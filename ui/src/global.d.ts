export {}

declare global {
  interface CoreEvent {
    method: string
    params: unknown
  }

  interface CoreApi {
    openExternal(url:string): Promise<void>
    pickFile(): Promise<string | null>
    launchGame(request: { instanceId: string; session: unknown }): Promise<{ started: boolean; alreadyRunning?: boolean; logPath?: string }>
    call<T = any>(method: string, params?: unknown): Promise<T>
    onEvent(cb: (e: CoreEvent) => void): () => void
  }

  interface Window {
    core: CoreApi
  }
}
