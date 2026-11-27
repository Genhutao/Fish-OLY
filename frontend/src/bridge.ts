// Wails runtime bindings are generated at build time.
// This thin adapter keeps components from importing generated code directly.
// In `wails dev` / `wails build`, the generated bindings live under
// ../bindings — we re-export the App methods with graceful fallbacks for
// browser-only development.

export interface Persona {
  id: string
  name: string
  avatar: string
  description: string
  personality: string
  coreAnchor: string
  tags: string[] | null
  createdAt: string
  updatedAt: string
  affection: number
  mood: string
  evoVersion: number
  presetId: string
  skillIds: string[] | null
}

export interface Skill {
  id: string
  name: string
  icon: string
  category: string
  description: string
  prompt: string
  source?: string
  builtin: boolean
  createdAt: string
}

export interface GitHubSkill {
  fullName: string
  name: string
  description: string
  url: string
  stars: number
  icon: string
}

export interface Group {
  id: string
  name: string
  icon: string
  kind: 'group' | 'script'
  sceneId: string
  topic: string
  memberIds: string[] | null
  messages: ChatMessage[] | null
  live: boolean
  createdAt: string
}

export interface Preset {
  id: string
  name: string
  icon: string
  description: string
  scenario: string
  style: string
  systemSnippet: string
  tags: string[] | null
  builtin: boolean
  createdAt: string
}

export interface RoamScene {
  id: string
  name: string
  icon: string
  description: string
  ambience: string
}

export interface RoamEvent {
  id: string
  personaId: string
  personaName: string
  avatar: string
  sceneId: string
  sceneName: string
  kind: 'move' | 'action' | 'rest' | 'wake' | 'post'
  content: string
  createdAt: string
}

export interface RoamPersona {
  personaId: string
  name: string
  avatar: string
  mood: string
  affection: number
  sceneId: string
  sceneName: string
  status: 'roaming' | 'resting'
  activity: string
  energy: number
}

export interface RoamView {
  running: boolean
  interval: number
  tick: number
  scenes: RoamScene[] | null
  personas: RoamPersona[] | null
  events: RoamEvent[] | null
}

export interface MemoryEntry {
  id: string
  personaId: string
  content: string
  tags: string[] | null
  important: number
  createdAt: string
}

export interface MemorySnapshot {
  id: string
  personaId: string
  version: number
  reason: string
  state: Persona
  createdAt: string
}

export interface ChatMessage {
  id: string
  sessionId: string
  role: string
  personaId?: string
  personaName?: string
  content: string
  strategy?: string
  kind?: 'text' | 'sticker' | 'voice' | 'system'
  meta?: string
  mock?: boolean
  createdAt: string
}

export interface StorySession {
  id: string
  title: string
  description: string
  personaIds: string[] | null
  userRole: string
  messages: ChatMessage[] | null
  createdAt: string
}

export interface OCPost {
  id: string
  authorId: string
  author: string
  avatar: string
  content: string
  tags: string[] | null
  likes: number
  comments: OCComment[] | null
  createdAt: string
  isAI: boolean
}

export interface OCComment {
  id: string
  authorId: string
  author: string
  avatar: string
  content: string
  createdAt: string
  isAI: boolean
}

export interface AppSettings {
  apiEndpoint: string
  apiKey: string
  model: string
  maxTokens: number
  temperature: number
  topP: number
  presencePenalty: number
  frequencyPenalty: number
  replyLength: 'short' | 'normal' | 'long'
  memoryTopK: number
  memoryWeightSim: number
  memoryWeightImp: number
  memoryWeightRec: number
  roamAutoPost: number
  roamRestThreshold: number
  roamEnergyDecay: number
  uiDensity: 'compact' | 'comfortable'
  reduceMotion: boolean
  typewriter: boolean
  enterToSend: boolean
  showTimestamps: boolean
  sceneBackground: boolean
  theme: 'dark' | 'light' | 'system'
  githubToken: string
}

export interface AppStats {
  personas: number
  memories: number
  posts: number
  stories: number
  skills: number
  presets: number
  roaming: number
  avgAffection: number
  ticks: number
}

export interface ChatRequest {
  personaId: string
  message: string
}

type Fn = (...args: any[]) => Promise<any>
const bridge: Record<string, Fn> =
  (window as any).go?.main?.App ?? {}

async function call(name: string, ...args: any[]): Promise<any> {
  const fn = bridge[name]
  if (!fn) {
    throw new Error(`backend method not bound: ${name}`)
  }
  return fn(...args)
}

export const api = {
  // persona
  createPersona: (p: Partial<Persona>) => call('CreatePersona', p),
  getPersona: (id: string) => call('GetPersona', id),
  listPersonas: (): Promise<Persona[]> => call('ListPersonas'),
  updatePersona: (p: Persona) => call('UpdatePersona', p),
  deletePersona: (id: string) => call('DeletePersona', id),
  aiExtractPersona: (raw: string): Promise<Persona> => call('AIExtractPersona', raw),
  // memory
  getSnapshots: (pid: string): Promise<MemorySnapshot[]> => call('GetSnapshots', pid),
  rollbackToSnapshot: (pid: string, sid: string): Promise<Persona> => call('RollbackToSnapshot', pid, sid),
  addMemory: (pid: string, content: string, imp: number, tags: string[]): Promise<MemoryEntry> => call('AddMemory', pid, content, imp, tags),
  getMemories: (pid: string): Promise<MemoryEntry[]> => call('GetMemories', pid),
  recallMemories: (pid: string, q: string, k: number): Promise<MemoryEntry[]> => call('RecallMemories', pid, q, k),
  // story
  createStory: (title: string, desc: string, pids: string[], userRole: string): Promise<StorySession> => call('CreateStory', title, desc, pids, userRole),
  getStory: (id: string): Promise<StorySession> => call('GetStory', id),
  listStories: (): Promise<StorySession[]> => call('ListStories'),
  sendStoryMessage: (sid: string, pid: string, msg: string): Promise<StorySession> => call('SendStoryMessage', sid, pid, msg),
  // community
  getPosts: (tag: string): Promise<OCPost[]> => call('GetPosts', tag),
  createPost: (pid: string, content: string, tags: string[]): Promise<OCPost> => call('CreatePost', pid, content, tags),
  likePost: (id: string) => call('LikePost', id),
  commentPost: (id: string, pid: string, content: string) => call('CommentPost', id, pid, content),
  // chat & settings
  chat: (req: ChatRequest): Promise<ChatMessage> => call('Chat', req),
  getPersonaMessages: (pid: string): Promise<ChatMessage[]> => call('GetPersonaMessages', pid),
  clearPersonaMessages: (pid: string): Promise<void> => call('ClearPersonaMessages', pid),
  getSettings: (): Promise<AppSettings> => call('GetSettings'),
  saveSettings: (s: AppSettings) => call('SaveSettings', s),
  // roaming
  getRoamState: (): Promise<RoamView> => call('GetRoamState'),
  setRoamRunning: (running: boolean): Promise<RoamView> => call('SetRoamRunning', running),
  setRoamInterval: (seconds: number): Promise<RoamView> => call('SetRoamInterval', seconds),
  roamStep: (): Promise<RoamView> => call('RoamStep'),
  // skill / preset library
  listSkills: (): Promise<Skill[]> => call('ListSkills'),
  createSkill: (s: Partial<Skill>): Promise<Skill> => call('CreateSkill', s),
  deleteSkill: (id: string) => call('DeleteSkill', id),
  listPresets: (): Promise<Preset[]> => call('ListPresets'),
  createPreset: (p: Partial<Preset>): Promise<Preset> => call('CreatePreset', p),
  updatePreset: (p: Preset): Promise<Preset> => call('UpdatePreset', p),
  deletePreset: (id: string) => call('DeletePreset', id),
  applyPreset: (personaId: string, presetId: string): Promise<Persona> => call('ApplyPreset', personaId, presetId),
  toggleSkill: (personaId: string, skillId: string): Promise<Persona> => call('ToggleSkill', personaId, skillId),
  // demo utilities
  getStats: (): Promise<AppStats> => call('GetStats'),
  testConnection: (): Promise<string> => call('TestConnection'),
  resetDemoData: () => call('ResetDemoData'),
  previewPersonaPrompt: (personaId: string): Promise<string> => call('PreviewPersonaPrompt', personaId),
  // group chats & script rooms
  listGroups: (): Promise<Group[]> => call('ListGroups'),
  getGroup: (id: string): Promise<Group> => call('GetGroup', id),
  sendGroupMessage: (id: string, content: string, kind: string, meta: string): Promise<Group> =>
    call('SendGroupMessage', id, content, kind, meta),
  setGroupsLive: (live: boolean) => call('SetGroupsLive', live),
  // github skill discovery
  searchGitHubSkills: (query: string): Promise<GitHubSkill[]> => call('SearchGitHubSkills', query),
  importGitHubSkill: (repo: string): Promise<Skill> => call('ImportGitHubSkill', repo),
}
