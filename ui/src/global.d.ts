export {}

declare global {
  interface CoreEvent {
    method: string
    params: unknown
  }

  interface CoreApi {
    call<T = any>(method: string, params?: unknown): Promise<T>
    onEvent(cb: (e: CoreEvent) => void): () => void
  }

  interface Window {
    core: CoreApi
  }
}