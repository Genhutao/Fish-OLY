const BASE = ''

export interface Persona {
  id: string
  name: string
  avatar: string
  anchor: string
  personality: string
  mood: string
  energy: number
  affection: number
  bond?: Record<string, number> | null
  scene: string
  activity: string
  lastSpoke: number
  sceneName?: string
  intentNow?: string
  plan?: Plan | null
}

export interface Message {
  id: string
  room: string
  actor: string
  name: string
  text: string
  at: string
  mock: boolean
  session?: string
  score?: number
}

export interface Scene {
  id: string
  name: string
  topic: string
  capacity: number
  visibility: string
  messages: Message[] | null
  here: string[] | null
}

export interface Intent {
  id: string
  sceneId: string
  start: number
  end: number
  kind: 'solo' | 'visit'
  target?: string
  status: string
}

export interface Plan {
  personaId: string
  day: number
  seed: number
  intents: Intent[]
}

export interface Session {
  id: string
  day: number
  sceneId: string
  sceneName?: string
  kind: 'solo' | 'pair'
  start: number
  end: number
  parts: { personaId: string; role: string }[]
  partNames?: string[]
  status: 'planned' | 'active' | 'ended' | 'cancelled' | 'notmet'
  said: number
  degraded?: boolean
  backfilled?: boolean
  summary?: string
}

export interface Member {
  id: string
  text: string
  day: number
  imp: number
  w: number
  refs?: string[]
  state: 'active' | 'forgotten'
}

export interface Slot {
  id: string
  layer: string
  text: string
  members: Member[]
  hits: number
  day: number
  lastDay: number
  imp0: number
  tags: string[] | null
  vec: string
}

export interface StatsView {
  slots: number
  members: number
  active: number
  forgotten: number
  writes: number
  dups: number
  merges: number
  appends: number
  slotsList: Slot[] | null
}

export interface WorldEvent {
  seq: number
  at: string
  type: string
  room?: string
  actor?: string
  text?: string
  data?: Record<string, unknown>
}

export interface Budget {
  used: number
  limit: number
  left: number
  over: boolean
}

export interface View {
  id: string
  tick: number
  day: number
  tickOfDay: number
  clock: string
  model: string
  events: number
  budget: Budget
  dormant: boolean
  personas: Persona[]
  scenes: Scene[]
  dms: Record<string, Message[]>
  sessions: Session[]
  memory: Record<string, StatsView>
  messageCount: number
  log: WorldEvent[]
}

export interface Hit {
  slotId: string
  layer: string
  text: string
  score: number
  tags?: string[]
  refs?: string[]
}

export interface ChatResult {
  reply: string
  mock: boolean
  hits: Hit[] | null
  memBlock: string
  remember: string
  error?: string
}

export interface UserInfo {
  id: string
  tick: number
  day: number
  model: string
}

async function jget<T>(path: string): Promise<T> {
  const r = await fetch(BASE + path)
  if (!r.ok) throw new Error(`${r.status} ${await r.text()}`)
  return r.json()
}

async function jpost<T>(path: string, body: unknown): Promise<T> {
  const r = await fetch(BASE + path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  })
  const data = await r.json()
  if (!r.ok) throw new Error(data?.error || data?.result?.error || `${r.status}`)
  return data
}

export const api = {
  users: () => jget<{ users: UserInfo[]; default: string }>('/api/v1/users'),
  createUser: (name: string) => jpost<{ ok: boolean; id: string }>('/api/v1/users', { name }),
  state: (uid: string) => jget<View>(`/api/v1/users/${encodeURIComponent(uid)}/state`),
  tick: (uid: string) => jpost<View>(`/api/v1/users/${encodeURIComponent(uid)}/tick`, {}),
  chat: (uid: string, personaId: string, text: string) =>
    jpost<{ result: ChatResult; view: View }>(`/api/v1/users/${encodeURIComponent(uid)}/chat`, {
      personaId,
      text,
    }),
  sceneChat: (uid: string, sceneId: string, text: string) =>
    jpost<{ result: ChatResult; view: View }>(
      `/api/v1/users/${encodeURIComponent(uid)}/scenes/${encodeURIComponent(sceneId)}/messages`,
      { text },
    ),
  search: (uid: string, personaId: string, q: string, k = 6) =>
    jget<{ query: string; personaId: string; hits: Hit[] }>(
      `/api/v1/users/${encodeURIComponent(uid)}/memory/search?personaId=${encodeURIComponent(
        personaId,
      )}&q=${encodeURIComponent(q)}&k=${k}`,
    ),
  forget: (uid: string, memberId: string) =>
    jpost<{ ok: boolean; view: View }>(`/api/v1/users/${encodeURIComponent(uid)}/memory/forget`, {
      memberId,
    }),
  revive: (uid: string, memberId: string) =>
    jpost<{ ok: boolean; view: View }>(`/api/v1/users/${encodeURIComponent(uid)}/memory/revive`, {
      memberId,
    }),
  consolidate: (uid: string) =>
    jpost<{ report: { merged: number; rebalanced: number; forgotten: number; slotsAfter: number }; view: View }>(
      `/api/v1/users/${encodeURIComponent(uid)}/memory/consolidate`,
      {},
    ),
}

// SSE：状态有变化就推；断线自动重连由 EventSource 负责，另加一层轮询兜底。
export function subscribe(uid: string, onState: (v: View) => void): () => void {
  let es: EventSource | null = null
  let timer: number | undefined

  const connect = () => {
    es = new EventSource(BASE + `/api/v1/users/${encodeURIComponent(uid)}/stream`)
    es.addEventListener('state', (ev) => {
      try {
        onState(JSON.parse((ev as MessageEvent).data))
      } catch {
        /* ignore */
      }
    })
    es.onerror = () => {
      es?.close()
      es = null
      window.setTimeout(connect, 2000)
    }
  }
  connect()

  timer = window.setInterval(async () => {
    if (es && es.readyState === EventSource.OPEN) return
    try {
      onState(await api.state(uid))
    } catch {
      /* server down */
    }
  }, 4000)

  return () => {
    es?.close()
    if (timer) window.clearInterval(timer)
  }
}
