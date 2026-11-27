import { useEffect, useMemo, useRef, useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { AnimatePresence, motion } from 'framer-motion'
import {
  Image as ImageIcon, Info, Mic, MoreHorizontal, Phone, Send, Smile, Sparkles,
  Square, Video, X,
} from 'lucide-react'
import { api, type AppSettings, type ChatMessage, type Group, type Persona } from '../bridge'
import { cn, useToast } from '../components/ui'
import { ChatList } from '../components/chat/ChatList'
import {
  CallOverlay, MessageBubble, ProfileDrawer, StickerPicker, TypingDots,
  dayLabel, previewOf, useStickerFavorites, type Convo,
} from '../components/chat/parts'

const MOOD: Record<string, string> = { happy: '愉快', calm: '平静', sad: '低落', angry: '恼火', tired: '疲惫', neutral: '平常' }

const GREETING: Record<string, string> = {
  'seed-1': '「……你来了。」她抬眼看你一下，又低下头去拨琴弦。',
  'seed-2': '「来得正好！我刚把光系咒文改到第三版——你要不要当第一个试验品？」',
}

export function ChatPage() {
  const toast = useToast()
  const nav = useNavigate()
  const [personas, setPersonas] = useState<Persona[]>([])
  const [groups, setGroups] = useState<Group[]>([])
  const [activeId, setActiveId] = useState('')
  const [localMsgs, setLocalMsgs] = useState<Record<string, ChatMessage[]>>({})
  const [settings, setSettings] = useState<AppSettings | null>(null)
  const [typing, setTyping] = useState(false)
  const [drawer, setDrawer] = useState(false)
  const [call, setCall] = useState<'voice' | 'video' | null>(null)
  const [input, setInput] = useState('')
  const [picker, setPicker] = useState(false)
  const [recording, setRecording] = useState(false)
  const [recordSec, setRecordSec] = useState(0)
  const [seen, setSeen] = useState<Record<string, string>>({})
  const scrollRef = useRef<HTMLDivElement>(null)
  const { favorites, collect } = useStickerFavorites()

  const load = () => {
    api.listPersonas().then(list => {
      setPersonas(list || [])
      setActiveId(prev => prev || (list[0] ? 'p:' + list[0].id : ''))
    }).catch(console.error)
    api.listGroups().then(list => setGroups(list || [])).catch(console.error)
  }
  useEffect(load, [])
  useEffect(() => { api.getSettings().then(setSettings).catch(() => {}) }, [])

  // Poll rooms so AI-to-AI chatter appears live.
  useEffect(() => {
    const t = setInterval(() => { api.listGroups().then(list => setGroups(list || [])).catch(() => {}) }, 3200)
    return () => clearInterval(t)
  }, [])

  const convos: Convo[] = useMemo(() => {
    const nowIso = new Date().toISOString()
    const pcs: Convo[] = personas.map(p => {
      const msgs = localMsgs['p:' + p.id] || []
      const last = msgs[msgs.length - 1]
      return {
        id: 'p:' + p.id, type: 'persona', name: p.name, avatar: p.avatar,
        subtitle: `${MOOD[p.mood] || p.mood} · ${p.coreAnchor}`,
        online: true, unread: 0,
        lastText: previewOf(last) || '开始聊天吧',
        lastAt: last?.createdAt || nowIso,
        persona: p,
      }
    })
    const gcs: Convo[] = groups.map(g => {
      const msgs = g.messages || []
      const last = msgs[msgs.length - 1]
      const isActive = activeId === 'g:' + g.id
      const mark = seen['g:' + g.id]
      const unread = isActive || !mark ? 0 : msgs.filter(m => m.role !== 'user' && +new Date(m.createdAt) > +new Date(mark)).length
      return {
        id: 'g:' + g.id, type: (g.kind === 'script' ? 'script' : 'group') as Convo['type'],
        name: g.name, avatar: g.icon,
        subtitle: `${(g.memberIds || []).length + 1} 人 · ${g.live ? 'AI 演绎中' : '已暂停'}`,
        online: g.live, unread,
        lastText: previewOf(last) || g.topic,
        lastAt: last?.createdAt || g.createdAt,
        group: g,
      }
    })
    return [...pcs, ...gcs]
  }, [personas, groups, localMsgs, activeId, seen])

  const active = convos.find(c => c.id === activeId)

  const messages: ChatMessage[] = useMemo(() => {
    if (!active) return []
    if (active.type === 'persona') return localMsgs[active.id] || []
    return active.group?.messages || []
  }, [active, localMsgs])

  // Load persisted history; fall back to a local greeting on first open.
  useEffect(() => {
    if (!active || active.type !== 'persona' || !active.persona) return
    const p = active.persona
    let alive = true
    const seed = () => {
      if (!alive) return
      setLocalMsgs(prev => {
        if ((prev[active.id] || []).length > 0) return prev
        return {
          ...prev,
          [active.id]: [{
            id: 'greet-' + p.id, sessionId: active.id, role: 'assistant',
            personaId: p.id, personaName: p.name, kind: 'text',
            content: GREETING[p.id] || `「${p.coreAnchor}」……你来了。`,
            createdAt: new Date().toISOString(),
          }],
        }
      })
    }
    api.getPersonaMessages(p.id)
      .then(hist => {
        if (!alive) return
        if (hist && hist.length > 0) setLocalMsgs(prev => ({ ...prev, [active.id]: hist }))
        else seed()
      })
      .catch(seed)
    return () => { alive = false }
  }, [active?.id])

  // Refresh AI status when settings are saved elsewhere.
  useEffect(() => {
    const h = () => api.getSettings().then(setSettings).catch(() => {})
    window.addEventListener('oly-settings', h)
    return () => window.removeEventListener('oly-settings', h)
  }, [])

  useEffect(() => {
    scrollRef.current?.scrollTo({ top: scrollRef.current.scrollHeight, behavior: 'smooth' })
  }, [messages.length, typing, activeId])

  useEffect(() => {
    if (active) setSeen(prev => ({ ...prev, [active.id]: new Date().toISOString() }))
  }, [activeId])

  useEffect(() => {
    if (!recording) return
    const t = setInterval(() => setRecordSec(s => s + 1), 1000)
    return () => clearInterval(t)
  }, [recording])

  const appendLocal = (id: string, m: ChatMessage) =>
    setLocalMsgs(prev => ({ ...prev, [id]: [...(prev[id] || []), m] }))

  const send = async (kind: 'text' | 'sticker' | 'voice' = 'text', content?: string, meta?: string) => {
    if (!active) return
    const text = (content ?? input).trim()
    if (!text && kind === 'text') return
    setInput('')

    const now = new Date().toISOString()
    const msg: ChatMessage = {
      id: 'local-' + Date.now(), sessionId: active.id, role: 'user',
      personaName: '我', content: text, kind, meta, createdAt: now,
    }

    if (active.type === 'persona' && active.persona) {
      appendLocal(active.id, msg)
      setTyping(true)
      try {
        const reply = await api.chat({ personaId: active.persona.id, message: kind === 'sticker' ? `[发来一个表情 ${text}]` : kind === 'voice' ? `[语音 ${meta} 秒]` : text })
        appendLocal(active.id, reply)
      } catch (e: any) {
        const errText = String(e?.message || '发送失败')
        appendLocal(active.id, {
          id: 'err-' + Date.now(), sessionId: active.id, role: 'system',
          content: errText, createdAt: new Date().toISOString(),
        })
        toast.push(errText, 'error')
      } finally { setTyping(false) }
      return
    }

    if (active.group) {
      try {
        const g = await api.sendGroupMessage(active.group.id, text, kind, meta || '')
        setGroups(prev => prev.map(x => x.id === g.id ? g : x))
      } catch (e: any) {
        toast.push(String(e?.message || '发送失败'), 'error')
      }
    }
  }

  const stopRecording = () => {
    setRecording(false)
    const sec = Math.max(1, recordSec)
    setRecordSec(0)
    send('voice', '语音消息', String(sec))
  }

  const insertEmoji = (e: string) => setInput(v => v + e)

  const enterSends = settings?.enterToSend ?? true

  return (
    <div className="flex h-[calc(100vh-44px)] overflow-hidden">
      <ChatList
        convos={convos}
        activeId={activeId}
        onSelect={id => { setActiveId(id); setDrawer(false) }}
        onOpenSettings={() => nav('/settings')}
      />

      {!active ? (
        <div className="grid flex-1 place-items-center text-[12px] text-muted">选择一个会话开始</div>
      ) : (
        <section className="flex min-w-0 flex-1 flex-col">
          {/* Header */}
          <header className="flex h-[66px] shrink-0 items-center gap-3 border-b border-border/70 px-5">
            <div className="grid h-9 w-9 place-items-center rounded-full border border-border bg-field text-lg">{active.avatar}</div>
            <div className="min-w-0">
              <div className="flex items-center gap-2">
                <h1 className="truncate font-serif text-[15.5px] text-bright">{active.name}</h1>
                {active.type !== 'persona' && (
                  <span className="tag text-[9px]">{active.type === 'script' ? '剧本' : '群聊'}</span>
                )}
                {active.group?.live && <span className="h-1.5 w-1.5 rounded-full bg-aurora animate-pulse-soft" title="AI 演绎中" />}
              </div>
              <p className="truncate text-[10.5px] text-muted">{active.subtitle}</p>
            </div>
            {active.type === 'persona' && (
              <span className={cn('tag text-[9px]', settings?.apiKey ? 'tag-aurora' : '')} title={settings?.apiKey ? '已接入模型' : '当前为内置 mock'}>
                {settings?.apiKey ? settings.model : 'mock'}
              </span>
            )}
            <div className="ml-auto flex items-center gap-1">
              <button className="btn-icon" aria-label="语音通话" title="语音通话" onClick={() => setCall('voice')}><Phone size={16} /></button>
              <button className="btn-icon" aria-label="视频通话" title="视频通话" onClick={() => setCall('video')}><Video size={17} /></button>
              <button className="btn-icon" aria-label="聊天资料" title="聊天资料" onClick={() => setDrawer(v => !v)}><Info size={16} /></button>
              <button className="btn-icon" aria-label="更多"><MoreHorizontal size={17} /></button>
            </div>
          </header>

          {/* Messages */}
          <div ref={scrollRef} className="flex-1 overflow-y-auto px-6 py-5">
            <div className="mx-auto max-w-[760px] space-y-4">
              {active.type === 'persona' && !settings?.apiKey && (
                <button onClick={() => nav('/settings')}
                  className="flex w-full items-center justify-between rounded-md border border-accent-300/25 bg-accent-300/[0.06] px-3 py-2 text-left text-[10.5px] text-muted hover:text-body">
                  <span>当前使用内置 mock 回复，还没有接入真实模型。</span>
                  <span className="text-accent-200">去设置接入 →</span>
                </button>
              )}
              {messages.map((m, i) => {
                const prev = messages[i - 1]
                const showDay = !prev || dayLabel(prev.createdAt) !== dayLabel(m.createdAt)
                const showName = m.role !== 'user' && m.role !== 'system' && (!prev || prev.personaId !== m.personaId || prev.role === 'system')
                return (
                  <div key={m.id}>
                    {showDay && (
                      <div className="my-4 flex items-center justify-center">
                        <span className="rounded-full border border-border/60 px-2.5 py-0.5 text-[9.5px] text-muted/60">{dayLabel(m.createdAt)}</span>
                      </div>
                    )}
                    <MessageBubble
                      m={m}
                      persona={active.persona}
                      showName={showName}
                      collected={!!m.meta && favorites.includes(m.meta)}
                      onCollect={emoji => {
                        const added = collect(emoji)
                        toast.push(added ? '已收藏这个表情' : '已经在收藏里了', added ? 'ok' : 'info')
                      }}
                    />
                  </div>
                )
              })}
              {typing && <TypingDots avatar={active.avatar} />}
            </div>
          </div>

          {/* Composer */}
          <div className="shrink-0 px-6 pb-5 pt-2">
            <div className="mx-auto max-w-[760px]">
              <div className="relative">
                <AnimatePresence>
                  {picker && (
                    <StickerPicker
                      favorites={favorites}
                      onEmoji={insertEmoji}
                      onSticker={e => { setPicker(false); send('sticker', e, e) }}
                      onClose={() => setPicker(false)}
                    />
                  )}
                </AnimatePresence>

                <div className="rounded-xl border border-border bg-field shadow-card transition-colors focus-within:border-accent-300/45">
                  <div className="flex items-center gap-1 px-2 pt-2">
                    <button className="btn-icon h-7 w-7" aria-label="表情" title="表情"
                      onClick={() => setPicker(p => !p)}><Smile size={15} /></button>
                    <button className="btn-icon h-7 w-7" aria-label="贴纸" title="贴纸"
                      onClick={() => setPicker(true)}><Sparkles size={14} /></button>
                    <button className="btn-icon h-7 w-7" aria-label="图片" title="图片（演示中不可用）" disabled><ImageIcon size={14} /></button>
                    <button className={cn('btn-icon h-7 w-7', recording && 'text-ember')} aria-label="语音" title="按住说话"
                      onClick={() => { if (recording) stopRecording(); else { setRecording(true); setRecordSec(0) } }}>
                      {recording ? <Square size={13} /> : <Mic size={15} />}
                    </button>
                    <span className="ml-auto text-[9.5px] text-muted/50">
                      {enterSends ? 'Enter 发送' : '⌘/Ctrl + Enter 发送'}
                    </span>
                  </div>

                  {recording ? (
                    <div className="flex items-center gap-3 px-3.5 py-3">
                      <span className="flex h-6 items-end gap-[3px]">
                        {[6, 12, 18, 10, 22, 14, 8, 16, 11, 19, 9, 13].map((h, i) => (
                          <span key={i} className="w-[2px] animate-wave rounded-full bg-ember/80" style={{ height: h, animationDelay: `${i * 70}ms` }} />
                        ))}
                      </span>
                      <span className="font-mono text-[12px] tabular-nums text-body">{String(Math.floor(recordSec / 60)).padStart(2, '0')}:{String(recordSec % 60).padStart(2, '0')}</span>
                      <span className="text-[10px] text-muted">正在录音…</span>
                      <div className="ml-auto flex gap-2">
                        <button onClick={() => { setRecording(false); setRecordSec(0) }} className="btn-ghost h-8 px-2 text-[11px]">取消</button>
                        <button onClick={stopRecording} className="btn-primary h-8 px-3 text-[11px]">发送</button>
                      </div>
                    </div>
                  ) : (
                    <textarea
                      rows={1}
                      value={input}
                      onChange={e => {
                        setInput(e.target.value)
                        e.target.style.height = 'auto'
                        e.target.style.height = Math.min(110, e.target.scrollHeight) + 'px'
                      }}
                      onKeyDown={e => {
                        if (e.key !== 'Enter') return
                        if (enterSends ? !e.shiftKey : (e.metaKey || e.ctrlKey)) {
                          e.preventDefault(); send()
                        }
                      }}
                      placeholder={active.type === 'persona' ? `发消息给 ${active.name}…` : '发消息到房间…'}
                      className="max-h-[110px] w-full resize-none bg-transparent px-3.5 py-2 text-[13px] leading-relaxed text-bright outline-none placeholder:text-muted/55"
                      aria-label="消息输入"
                    />
                  )}

                  {!recording && (
                    <div className="flex items-center justify-between px-3 pb-2.5">
                      <span className="text-[9.5px] text-muted/45">Shift + Enter 换行</span>
                      <button
                        onClick={() => send()}
                        disabled={!input.trim()}
                        className="btn-primary h-8 px-4 text-[11.5px]"
                      >
                        发送 <Send size={11} />
                      </button>
                    </div>
                  )}
                </div>
              </div>
            </div>
          </div>
        </section>
      )}

      <AnimatePresence>
        {drawer && active && (
          <ProfileDrawer
            convo={active}
            onClose={() => setDrawer(false)}
            onCall={mode => { setDrawer(false); setCall(mode) }}
          />
        )}
      </AnimatePresence>

      <AnimatePresence>
        {call && active && (
          <CallOverlay convo={active} mode={call} onClose={() => setCall(null)} />
        )}
      </AnimatePresence>
    </div>
  )
}
