import { useEffect, useMemo, useRef, useState } from 'react'
import { Link, useNavigate, useParams } from 'react-router-dom'
import {
  ArrowLeft, Brain, Check, ChevronRight, Copy, Heart, PackageCheck, Send,
  Sparkles, Trash2, Wand2,
} from 'lucide-react'
import { api, type AppSettings, type ChatMessage, type MemoryEntry, type Persona, type Preset, type Skill } from '../bridge'
import { Button, IconButton, Meter, Modal, cn, useToast } from '../components/ui'

const MOOD_LABEL: Record<string, string> = {
  happy: '愉快', calm: '平静', sad: '低落', angry: '恼火', tired: '疲惫', neutral: '平常',
}
const MOOD_DOT: Record<string, string> = {
  happy: 'bg-mood-happy', calm: 'bg-mood-calm', sad: 'bg-mood-sad',
  angry: 'bg-mood-angry', tired: 'bg-mood-tired', neutral: 'bg-mood-neutral',
}

const OPENERS: Record<string, string[]> = {
  default: ['今晚的月色不错，你在做什么？', '我最近总梦见同一个地方。', '有件事，我只想和你说。'],
  'seed-1': ['庭院的桂花开了，你闻到了吗？', '你的剑，今晚为什么没有出鞘？'],
  'seed-2': ['学院今天又炸了一个魔法炉。', '你能教我那个光系咒文吗？'],
}

function useTypewriter(text: string, enabled: boolean) {
  const [shown, setShown] = useState(enabled ? '' : text)
  useEffect(() => {
    if (!enabled) { setShown(text); return }
    setShown('')
    let i = 0
    const step = Math.max(1, Math.round(text.length / 90))
    const timer = window.setInterval(() => {
      i = Math.min(text.length, i + step)
      setShown(text.slice(0, i))
      if (i >= text.length) window.clearInterval(timer)
    }, 16)
    return () => window.clearInterval(timer)
  }, [text, enabled])
  return shown
}

export function PersonaDetailPage() {
  const { id } = useParams<{ id: string }>()
  const nav = useNavigate()
  const toast = useToast()
  const [persona, setPersona] = useState<Persona | null>(null)
  const [messages, setMessages] = useState<ChatMessage[]>([])
  const [input, setInput] = useState('')
  const [sending, setSending] = useState(false)
  const [preset, setPreset] = useState<Preset | null>(null)
  const [skills, setSkills] = useState<Skill[]>([])
  const [memories, setMemories] = useState<MemoryEntry[]>([])
  const [settings, setSettings] = useState<AppSettings | null>(null)
  const [promptOpen, setPromptOpen] = useState(false)
  const [promptText, setPromptText] = useState('')
  const [lastReplyId, setLastReplyId] = useState('')
  const scrollRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!id) return
    api.getPersona(id).then(setPersona).catch(console.error)
    api.listPresets().then(list => setPreset(list.find(p => p.id === persona?.presetId) || null)).catch(console.error)
    api.listSkills().then(setSkills).catch(console.error)
    api.getMemories(id).then(setMemories).catch(console.error)
    api.getSettings().then(setSettings).catch(console.error)
  }, [id])

  useEffect(() => {
    if (!persona) return
    api.listPresets().then(list => setPreset(list.find(p => p.id === persona.presetId) || null)).catch(console.error)
  }, [persona?.presetId])

  useEffect(() => {
    scrollRef.current?.scrollTo({ top: scrollRef.current.scrollHeight, behavior: 'smooth' })
  }, [messages, sending])

  const equipped = useMemo(
    () => (persona?.skillIds || []).map(sid => skills.find(s => s.id === sid)).filter(Boolean) as Skill[],
    [persona?.skillIds, skills],
  )

  const openPrompt = async () => {
    if (!id) return
    try {
      const text = await api.previewPersonaPrompt(id)
      setPromptText(text)
      setPromptOpen(true)
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
  }

  const send = async () => {
    if (!input.trim() || !id || sending) return
    const text = input.trim()
    setInput('')
    setSending(true)
    setMessages(prev => [...prev, {
      id: 'local-' + Date.now(), sessionId: id, role: 'user', personaName: '我',
      content: text, createdAt: new Date().toISOString(),
    }])
    try {
      const reply = await api.chat({ personaId: id, message: text })
      setMessages(prev => [...prev, reply])
      setLastReplyId(reply.id)
    } catch (e: any) {
      toast.push(String(e?.message || '发送失败'), 'error')
    } finally { setSending(false) }
  }

  const remove = async () => {
    if (!id) return
    await api.deletePersona(id)
    nav('/personas')
  }

  const copy = async (text: string) => {
    try { await navigator.clipboard.writeText(text); toast.push('已复制') }
    catch { toast.push('复制失败', 'error') }
  }

  if (!persona) {
    return <div className="px-8 py-10 text-[12px] text-muted">正在读取人设…</div>
  }

  const enterSends = settings?.enterToSend ?? true
  const typewriter = (settings?.typewriter ?? true) && lastReplyId !== ''

  return (
    <div className="flex h-[calc(100vh-44px)] flex-col">
      {/* Header */}
      <header className="shrink-0 border-b border-border/70 px-7 py-4">
        <div className="flex items-center gap-4">
          <IconButton label="返回人设列表" onClick={() => nav('/personas')}><ArrowLeft size={16} /></IconButton>
          <div className="grid h-11 w-11 place-items-center rounded-full border border-accent-300/25 bg-accent-300/[0.06] text-xl">
            {persona.avatar || '🌙'}
          </div>
          <div className="min-w-0 flex-1">
            <div className="flex items-center gap-3">
              <h1 className="display text-[21px] leading-tight">{persona.name}</h1>
              <span className="flex items-center gap-1.5 text-[10.5px] text-muted">
                <span className={cn('h-1.5 w-1.5 rounded-full', MOOD_DOT[persona.mood] || 'bg-muted')} />
                {MOOD_LABEL[persona.mood] || persona.mood}
              </span>
              <span className="tag">v{persona.evoVersion}</span>
            </div>
            <p className="quote mt-1 truncate text-[12px]">「{persona.coreAnchor || '未设定核心锚点'}」</p>
          </div>
          <Link to={`/memory/${id}`}><Button variant="outline" size="sm" icon={<Brain size={13} />}>记忆</Button></Link>
          <Button variant="ghost" size="sm" onClick={openPrompt} icon={<Sparkles size={13} />}>提示词</Button>
          <IconButton label="删除人设" onClick={remove} className="hover:text-ember"><Trash2 size={15} /></IconButton>
        </div>
      </header>

      <div className="flex min-h-0 flex-1">
        {/* Chat column */}
        <div className="flex min-w-0 flex-1 flex-col">
          <div ref={scrollRef} className="flex-1 overflow-y-auto px-7 py-6">
            <div className="mx-auto max-w-[720px] space-y-5">
              {messages.length === 0 && (
                <div className="py-10 text-center">
                  <div className="mx-auto mb-4 grid h-16 w-16 place-items-center rounded-full border border-accent-300/20 bg-accent-300/[0.05] text-3xl">
                    {persona.avatar || '🌙'}
                  </div>
                  <p className="display text-[16px]">对 {persona.name} 说第一句话</p>
                  <p className="mx-auto mt-1.5 max-w-sm text-[12px] leading-relaxed text-muted">
                    记忆、预设与已装备技能会一起进入这一轮的提示词。
                  </p>
                  <div className="mt-5 flex flex-wrap justify-center gap-2">
                    {(OPENERS[persona.id] || OPENERS.default).map(t => (
                      <button key={t} onClick={() => setInput(t)}
                        className="h-8 rounded-full border border-border px-3 text-[11.5px] text-body/75 transition-colors hover:border-accent-300/35 hover:text-bright">
                        {t}
                      </button>
                    ))}
                  </div>
                </div>
              )}

              {messages.map(m => {
                const isUser = m.role === 'user'
                const isLast = m.id === lastReplyId
                const content = isLast && typewriter ? <Typewriter text={m.content} /> : m.content
                return (
                  <div key={m.id} className={cn('group flex gap-3', isUser ? 'justify-end' : 'justify-start')}>
                    {!isUser && (
                      <div className="mt-1 grid h-8 w-8 shrink-0 place-items-center rounded-full border border-border bg-field text-base">
                        {persona.avatar || '🌙'}
                      </div>
                    )}
                    <div className={cn('max-w-[78%]', isUser && 'items-end')}>
                      <p className={cn('mb-1 text-[10px] text-muted/60', isUser ? 'text-right' : '')}>
                        {m.personaName || (isUser ? '我' : persona.name)}
                        {settings?.showTimestamps && (
                          <span className="ml-2 opacity-0 transition-opacity group-hover:opacity-100">
                            {new Date(m.createdAt).toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })}
                          </span>
                        )}
                      </p>
                      <div className={cn(
                        'relative rounded-lg border px-4 py-2.5 text-[13px] leading-[1.75]',
                        isUser
                          ? 'border-accent-300/30 bg-accent-300/[0.10] text-bright'
                          : 'border-border bg-raised/90 text-body',
                      )}>
                        {content}
                        {!isUser && (
                          <button
                            onClick={() => copy(m.content)}
                            aria-label="复制回复"
                            className="absolute -right-8 top-1 rounded p-1 text-muted opacity-0 transition-opacity hover:text-bright group-hover:opacity-100"
                          >
                            <Copy size={12} />
                          </button>
                        )}
                      </div>
                    </div>
                  </div>
                )
              })}

              {sending && (
                <div className="flex items-center gap-3">
                  <div className="grid h-8 w-8 place-items-center rounded-full border border-border bg-field text-base">{persona.avatar || '🌙'}</div>
                  <div className="flex items-center gap-1 rounded-lg border border-border bg-raised/90 px-4 py-3">
                    {[0, 120, 240].map(d => (
                      <span key={d} className="h-1.5 w-1.5 animate-bounce rounded-full bg-accent-300/70" style={{ animationDelay: `${d}ms` }} />
                    ))}
                  </div>
                </div>
              )}
            </div>
          </div>

          {/* Composer */}
          <div className="shrink-0 border-t border-border/70 px-7 py-4">
            <div className="mx-auto max-w-[720px]">
              <div className="rounded-lg border border-border bg-field transition-colors focus-within:border-accent-300/45">
                <textarea
                  rows={1}
                  value={input}
                  onChange={e => {
                    setInput(e.target.value)
                    e.target.style.height = 'auto'
                    e.target.style.height = Math.min(120, e.target.scrollHeight) + 'px'
                  }}
                  onKeyDown={e => {
                    if (e.key !== 'Enter') return
                    const shouldSend = enterSends ? !e.shiftKey : (e.metaKey || e.ctrlKey)
                    if (shouldSend) { e.preventDefault(); send() }
                  }}
                  placeholder={`对 ${persona.name} 说…`}
                  aria-label="输入消息"
                  disabled={sending}
                  className="max-h-[120px] w-full resize-none bg-transparent px-4 py-3 text-[13px] leading-relaxed text-bright outline-none placeholder:text-muted/60"
                />
                <div className="flex items-center justify-between px-3 pb-2.5">
                  <span className="text-[10px] text-muted/50">
                    {enterSends ? 'Enter 发送 · Shift + Enter 换行' : '⌘/Ctrl + Enter 发送'}
                  </span>
                  <Button size="sm" onClick={send} disabled={!input.trim()} loading={sending} icon={<Send size={12} />}>
                    发送
                  </Button>
                </div>
              </div>
            </div>
          </div>
        </div>

        {/* Profile rail */}
        <aside className="hidden w-[292px] shrink-0 overflow-y-auto border-l border-border/70 bg-panel/60 px-5 py-5 lg:block">
          <section>
            <p className="section-title">状态</p>
            <div className="mb-3 flex items-end justify-between">
              <span className="flex items-center gap-1.5 text-[12px] text-body/80"><Heart size={13} className="text-accent-300" /> 好感度</span>
              <span className="font-mono text-[13px] tabular-nums text-bright">{persona.affection.toFixed(1)}<span className="text-muted">/10</span></span>
            </div>
            <Meter value={persona.affection + 10} max={20} />
            <p className="mt-2 text-[10.5px] leading-relaxed text-muted/70">
              好感度与心情会随对话缓慢变化，并在记忆中留下痕迹。
            </p>
          </section>

          <div className="my-5 h-px bg-border/70" />

          <section>
            <p className="section-title flex items-center justify-between">
              装配 <Link to="/library" className="text-[10px] font-normal text-muted hover:text-accent-200">调整</Link>
            </p>
            <div className="rounded-md border border-border bg-veil/[0.03] px-3 py-2.5">
              <div className="flex items-center gap-2">
                <Wand2 size={13} className="text-accent-300" />
                <span className="text-[12px] text-body">{preset ? `${preset.icon} ${preset.name}` : '未套用预设'}</span>
              </div>
              {preset?.scenario && <p className="mt-1.5 text-[10.5px] leading-relaxed text-muted">{preset.scenario}</p>}
            </div>
            <div className="mt-2 space-y-1.5">
              {equipped.length === 0 && <p className="text-[10.5px] text-muted/60">尚未装备技能</p>}
              {equipped.map(s => (
                <div key={s.id} className="flex items-center gap-2 rounded-md border border-border/70 px-2.5 py-1.5">
                  <PackageCheck size={12} className="text-aurora" />
                  <span className="flex-1 truncate text-[11px] text-body/80">{s.icon} {s.name}</span>
                </div>
              ))}
            </div>
            <button onClick={openPrompt} className="mt-2 flex w-full items-center justify-between rounded-md px-2.5 py-1.5 text-[10.5px] text-muted hover:text-accent-200">
              预览组装后的提示词 <ChevronRight size={12} />
            </button>
          </section>

          <div className="my-5 h-px bg-border/70" />

          <section>
            <p className="section-title flex items-center justify-between">
              最近记忆 <Link to={`/memory/${id}`} className="text-[10px] font-normal text-muted hover:text-accent-200">全部</Link>
            </p>
            {memories.length === 0 ? (
              <p className="text-[10.5px] leading-relaxed text-muted/60">对话超过 20 字会写入一条记忆。</p>
            ) : memories.slice(0, 3).map(m => (
              <div key={m.id} className="mb-2 rounded-md border border-border/60 px-2.5 py-2">
                <p className="line-clamp-2 text-[10.5px] leading-relaxed text-body/70">{m.content}</p>
                <p className="mt-1 text-[9.5px] text-muted/50">重要度 {m.important.toFixed(1)}</p>
              </div>
            ))}
          </section>
        </aside>
      </div>

      <Modal
        open={promptOpen}
        onClose={() => setPromptOpen(false)}
        title="这一轮会发送的提示词"
        description="由人设、预设、技能与记忆装配而成；实际请求时记忆会替换为检索结果。"
        width="max-w-2xl"
        footer={<Button variant="outline" onClick={() => copy(promptText)} icon={<Copy size={13} />}>复制</Button>}
      >
        <pre className="whitespace-pre-wrap break-words font-mono text-[11px] leading-relaxed text-body/75">{promptText}</pre>
      </Modal>
    </div>
  )
}

function Typewriter({ text }: { text: string }) {
  const shown = useTypewriter(text, true)
  return <>{shown}<span className="ml-0.5 inline-block h-3 w-[1px] animate-pulse bg-accent-300/60 align-middle" /></>
}
