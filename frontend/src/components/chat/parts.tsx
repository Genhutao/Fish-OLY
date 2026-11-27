import { useEffect, useMemo, useState } from 'react'
import { motion } from 'framer-motion'
import {
  Mic, MicOff, Phone, PhoneOff, Play, Star, Video, Volume2, VolumeX, X,
} from 'lucide-react'
import { api, type ChatMessage, type Group, type MemoryEntry, type Persona, type Preset, type Skill } from '../../bridge'
import { cn, useToast } from '../ui'

export type Convo = {
  id: string
  type: 'persona' | 'group' | 'script'
  name: string
  avatar: string
  subtitle: string
  online: boolean
  unread: number
  lastText: string
  lastAt: string
  group?: Group
  persona?: Persona
}

export function previewOf(m?: ChatMessage | null) {
  if (!m) return ''
  if (m.kind === 'sticker') return '[表情]'
  if (m.kind === 'voice') return `[语音] ${m.meta || ''}"`
  return m.content
}

export const EMOJI = [
  '😀', '😄', '😊', '🥰', '😍', '😘', '😜', '🤪', '🤔', '😐',
  '😴', '😪', '😢', '😭', '😤', '😡', '🥺', '😳', '🤯', '🥳',
  '😎', '🤓', '🫠', '🙃', '🌙', '✨', '🌸', '🍂', '❄️', '🌧️',
  '🔥', '💧', '⚔️', '🗡️', '🎼', '📜', '📚', '🕯️', '🫖', '🍡',
]

export const STICKERS = [
  '🌙', '☀️', '✨', '🌧️', '❄️', '🔥', '💧', '⚔️', '🗡️', '🎼',
  '📜', '📚', '🕯️', '🫖', '🍡', '🌸', '🍂', '🐈', '🦊', '🐇',
]

const FAV_KEY = 'oly-sticker-favorites'

export function useStickerFavorites() {
  const [favorites, setFavorites] = useState<string[]>(() => {
    try { return JSON.parse(localStorage.getItem(FAV_KEY) || '[]') } catch { return [] }
  })
  const save = (next: string[]) => {
    setFavorites(next)
    try { localStorage.setItem(FAV_KEY, JSON.stringify(next)) } catch { /* ignore */ }
  }
  const collect = (emoji: string) => {
    if (favorites.includes(emoji)) return false
    save([...favorites, emoji].slice(-40))
    return true
  }
  const remove = (emoji: string) => save(favorites.filter(e => e !== emoji))
  return { favorites, collect, remove }
}

/* ─── Emoji / sticker picker ──────────────────────────────────────────────── */

export function StickerPicker({
  favorites, onEmoji, onSticker, onClose,
}: {
  favorites: string[]
  onEmoji: (e: string) => void
  onSticker: (e: string) => void
  onClose: () => void
}) {
  const [tab, setTab] = useState<'emoji' | 'stickers' | 'fav'>('emoji')
  return (
    <motion.div
      initial={{ opacity: 0, y: 8, scale: 0.98 }}
      animate={{ opacity: 1, y: 0, scale: 1 }}
      exit={{ opacity: 0, y: 6, scale: 0.98 }}
      transition={{ type: 'spring', stiffness: 420, damping: 32 }}
      className="card absolute bottom-full right-0 z-30 mb-2 w-[330px] overflow-hidden p-3 shadow-modal"
    >
      <div className="mb-2 flex items-center gap-1">
        {([['emoji', '表情'], ['stickers', '贴纸'], ['fav', '收藏']] as const).map(([k, label]) => (
          <button key={k} onClick={() => setTab(k)}
            className={cn('rounded-md px-2.5 py-1 text-[11px] transition-colors',
              tab === k ? 'bg-accent-300/12 text-accent-200' : 'text-muted hover:text-body')}>
            {label}{k === 'fav' && favorites.length > 0 ? ` ${favorites.length}` : ''}
          </button>
        ))}
        <button onClick={onClose} className="ml-auto rounded p-1 text-muted hover:text-body" aria-label="关闭表情面板">
          <X size={13} />
        </button>
      </div>
      <div className="grid max-h-[210px] grid-cols-8 gap-1 overflow-y-auto pr-1">
        {(tab === 'emoji' ? EMOJI : tab === 'stickers' ? STICKERS : favorites).map((e, i) => (
          <motion.button
            key={e + i}
            whileHover={{ scale: 1.12 }}
            whileTap={{ scale: 0.94 }}
            onClick={() => (tab === 'emoji' ? onEmoji(e) : onSticker(e))}
            className={cn('grid place-items-center rounded-md hover:bg-veil/[0.06]',
              tab === 'emoji' ? 'h-8 text-lg' : 'h-12 text-3xl')}
          >
            {e}
          </motion.button>
        ))}
        {tab === 'fav' && favorites.length === 0 && (
          <p className="col-span-8 py-6 text-center text-[11px] text-muted">在表情消息上点收藏，就会出现在这里。</p>
        )}
      </div>
    </motion.div>
  )
}

/* ─── Voice / video call overlay ──────────────────────────────────────────── */

export function CallOverlay({
  convo, mode, onClose,
}: { convo: Convo; mode: 'voice' | 'video'; onClose: () => void }) {
  const [connected, setConnected] = useState(false)
  const [seconds, setSeconds] = useState(0)
  const [muted, setMuted] = useState(false)
  const [speaker, setSpeaker] = useState(true)
  const [videoOn, setVideoOn] = useState(mode === 'video')

  useEffect(() => {
    const t = setTimeout(() => setConnected(true), 1600)
    return () => clearTimeout(t)
  }, [])
  useEffect(() => {
    if (!connected) return
    const t = setInterval(() => setSeconds(s => s + 1), 1000)
    return () => clearInterval(t)
  }, [connected])

  const mm = String(Math.floor(seconds / 60)).padStart(2, '0')
  const ss = String(seconds % 60).padStart(2, '0')

  return (
    <motion.div
      initial={{ opacity: 0 }}
      animate={{ opacity: 1 }}
      exit={{ opacity: 0 }}
      transition={{ duration: 0.22 }}
      className="fixed inset-0 z-[70] flex flex-col items-center justify-center overflow-hidden"
      role="dialog"
      aria-label="通话"
    >
      <div className="absolute inset-0 bg-[#0b0d14]" />
      {videoOn && (
        <div
          className="absolute inset-0 opacity-40 blur-2xl"
          style={{ background: 'radial-gradient(60% 60% at 50% 40%, rgb(var(--c-accent-300) / 0.35), transparent 70%), radial-gradient(50% 50% at 70% 80%, rgb(var(--c-aurora) / 0.25), transparent 70%)' }}
        />
      )}
      <div className="absolute inset-0 opacity-[0.05]"
        style={{ backgroundImage: 'radial-gradient(circle at 1px 1px, #fff 1px, transparent 0)', backgroundSize: '28px 28px' }} />

      <div className="relative z-10 flex flex-col items-center">
        <div className="relative mb-7 grid place-items-center">
          <span className="absolute h-[150px] w-[150px] rounded-full border border-accent-300/30 animate-ring" />
          <span className="absolute h-[150px] w-[150px] rounded-full border border-accent-300/20 animate-ring" style={{ animationDelay: '1.1s' }} />
          <div className="grid h-[118px] w-[118px] place-items-center rounded-full border border-accent-300/35 bg-[#11131b] text-6xl shadow-glow">
            {convo.avatar}
          </div>
        </div>
        <h2 className="font-serif text-[26px] text-[#f4efe4]">{convo.name}</h2>
        <p className="mt-1.5 font-mono text-[13px] tabular-nums text-[#c9c3b4]/70">
          {connected ? `${mm}:${ss}` : '正在呼叫…'}
        </p>
        <p className="mt-1 text-[11px] text-[#c9c3b4]/45">{mode === 'video' ? '视频通话 · 演示' : '语音通话 · 演示'}</p>

        <div className="mt-10 flex items-center gap-5">
          {[
            { icon: muted ? MicOff : Mic, on: muted, label: muted ? '取消静音' : '静音', act: () => setMuted(v => !v) },
            { icon: speaker ? Volume2 : VolumeX, on: !speaker, label: '扬声器', act: () => setSpeaker(v => !v) },
            { icon: Video, on: videoOn, label: '摄像头', act: () => setVideoOn(v => !v) },
          ].map(({ icon: Icon, on, label, act }) => (
            <motion.button
              key={label}
              whileTap={{ scale: 0.92 }}
              onClick={act}
              aria-label={label}
              className={cn('grid h-12 w-12 place-items-center rounded-full border transition-colors',
                on ? 'border-accent-300/60 bg-accent-300/20 text-accent-200' : 'border-white/10 bg-white/[0.06] text-[#efe9dc]/75 hover:bg-white/[0.1]')}
            >
              <Icon size={18} />
            </motion.button>
          ))}
          <motion.button
            whileHover={{ scale: 1.05 }}
            whileTap={{ scale: 0.94 }}
            onClick={onClose}
            aria-label="挂断"
            className="grid h-14 w-14 place-items-center rounded-full bg-[#d1594c] text-white shadow-[0_12px_30px_-12px_rgba(209,89,76,0.8)]"
          >
            <PhoneOff size={22} />
          </motion.button>
        </div>
      </div>
    </motion.div>
  )
}

/* ─── Profile drawer ──────────────────────────────────────────────────────── */

export function ProfileDrawer({
  convo, onClose, onCall,
}: { convo: Convo; onClose: () => void; onCall: (mode: 'voice' | 'video') => void }) {
  const toast = useToast()
  const [preset, setPreset] = useState<Preset | null>(null)
  const [skills, setSkills] = useState<Skill[]>([])
  const [memories, setMemories] = useState<MemoryEntry[]>([])

  useEffect(() => {
    if (convo.type !== 'persona' || !convo.persona) return
    api.listPresets().then(list => setPreset(list.find(p => p.id === convo.persona?.presetId) || null)).catch(() => {})
    api.listSkills().then(setSkills).catch(() => {})
    api.getMemories(convo.persona.id).then(setMemories).catch(() => {})
  }, [convo.type, convo.persona?.id])

  const equipped = (convo.persona?.skillIds || []).map(id => skills.find(s => s.id === id)).filter(Boolean) as Skill[]

  return (
    <>
      <motion.div
        initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}
        className="fixed inset-0 z-30 bg-[rgb(var(--c-scrim)/0.35)] backdrop-blur-[2px]"
        onClick={onClose}
      />
      <motion.aside
        initial={{ x: 360, opacity: 0.6 }}
        animate={{ x: 0, opacity: 1 }}
        exit={{ x: 360, opacity: 0.4 }}
        transition={{ type: 'spring', stiffness: 340, damping: 34, mass: 0.9 }}
        className="fixed bottom-0 right-0 top-11 z-40 w-[330px] overflow-y-auto border-l border-border bg-raised px-5 py-5 shadow-modal"
        aria-label="角色资料"
      >
        <div className="flex justify-end">
          <button onClick={onClose} className="btn-icon h-7 w-7" aria-label="关闭资料卡"><X size={14} /></button>
        </div>

        <div className="flex flex-col items-center text-center">
          <div className="grid h-16 w-16 place-items-center rounded-full border border-accent-300/25 bg-accent-300/[0.06] text-3xl">
            {convo.avatar}
          </div>
          <h2 className="mt-3 font-serif text-[19px] text-bright">{convo.name}</h2>
          <p className="mt-1 text-[11px] text-muted">{convo.subtitle}</p>
          {convo.type === 'persona' && convo.persona && (
            <p className="quote mt-2 text-[11px]">「{convo.persona.coreAnchor}」</p>
          )}
        </div>

        {convo.type === 'persona' && convo.persona ? (
          <>
            <div className="mt-5 flex gap-2">
              <button onClick={() => onCall('voice')} className="btn-outline h-8 flex-1 text-[11.5px]"><Phone size={12} /> 语音</button>
              <button onClick={() => onCall('video')} className="btn-outline h-8 flex-1 text-[11.5px]"><Video size={12} /> 视频</button>
            </div>

            <div className="mt-5">
              <p className="section-title">好感度</p>
              <div className="flex items-center gap-2">
                <span className="h-[4px] flex-1 overflow-hidden rounded-full bg-track">
                  <span className="block h-full rounded-full bg-accent-300" style={{ width: `${Math.max(4, ((convo.persona.affection + 10) / 20) * 100)}%` }} />
                </span>
                <span className="font-mono text-[11px] text-body/80">{convo.persona.affection.toFixed(1)}</span>
              </div>
            </div>

            <div className="mt-4">
              <p className="section-title">装配</p>
              <div className="rounded-md border border-border bg-field px-3 py-2">
                <p className="text-[11.5px] text-body">{preset ? `${preset.icon} ${preset.name}` : '未套用预设'}</p>
                {preset?.scenario && <p className="mt-1 text-[10px] text-muted">{preset.scenario}</p>}
              </div>
              <div className="mt-1.5 flex flex-wrap gap-1.5">
                {equipped.map(s => <span key={s.id} className="tag text-[10px]">{s.icon} {s.name}</span>)}
                {equipped.length === 0 && <span className="text-[10px] text-muted">尚未装备技能</span>}
              </div>
            </div>

            <div className="mt-4">
              <p className="section-title">最近记忆</p>
              {memories.slice(0, 3).map(m => (
                <div key={m.id} className="mb-1.5 rounded-md border border-border/70 px-2.5 py-2">
                  <p className="line-clamp-2 text-[10.5px] leading-relaxed text-body/75">{m.content}</p>
                </div>
              ))}
              {memories.length === 0 && <p className="text-[10.5px] text-muted">还没有记忆。</p>}
            </div>

            <button
              onClick={() => { navigator.clipboard?.writeText(convo.persona?.coreAnchor || ''); toast.push('核心锚点已复制') }}
              className="btn-ghost mt-3 h-8 w-full text-[11px]"
            >
              复制核心锚点
            </button>
          </>
        ) : convo.group ? (
          <>
            <div className="mt-5">
              <p className="section-title">房间信息</p>
              <p className="text-[11.5px] leading-relaxed text-body/80">{convo.group.topic}</p>
              <div className="mt-2 flex flex-wrap gap-1.5">
                <span className="tag text-[10px]">{convo.group.kind === 'script' ? '剧本' : '群聊'}</span>
                <span className="tag text-[10px]">{convo.group.messages?.length || 0} 条消息</span>
                <span className="tag text-[10px]">{convo.group.live ? 'AI 自动演绎中' : '已暂停'}</span>
              </div>
            </div>
            <div className="mt-4">
              <p className="section-title">成员</p>
              <div className="space-y-1.5">
                {(convo.group.memberIds || []).map(id => (
                  <div key={id} className="flex items-center gap-2 rounded-md border border-border/70 px-2.5 py-2">
                    <span className="text-base">🎭</span>
                    <span className="text-[11.5px] text-body">{personaName(convo, id)}</span>
                  </div>
                ))}
              </div>
            </div>
            <div className="mt-4 flex gap-2">
              <button onClick={() => onCall('voice')} className="btn-outline h-8 flex-1 text-[11.5px]"><Phone size={12} /> 语音</button>
              <button onClick={() => onCall('video')} className="btn-outline h-8 flex-1 text-[11.5px]"><Video size={12} /> 视频</button>
            </div>
          </>
        ) : null}
      </motion.aside>
    </>
  )
}

function personaName(convo: Convo, id: string) {
  const g = convo.group
  if (!g) return id
  return id === 'seed-1' ? '苍月千鹤' : id === 'seed-2' ? '白昼·艾尔' : '成员'
}

/* ─── Message bubble ──────────────────────────────────────────────────────── */

export function MessageBubble({
  m, persona, showName, onCollect, collected,
}: {
  m: ChatMessage
  persona?: Persona
  showName: boolean
  onCollect: (emoji: string) => void
  collected: boolean
}) {
  const [playing, setPlaying] = useState(false)
  const [copied, setCopied] = useState(false)
  const isUser = m.role === 'user'
  const isSystem = m.role === 'system' || m.kind === 'system'
  const time = new Date(m.createdAt).toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit' })

  if (isSystem) {
    return (
      <div className="my-3 flex items-center justify-center">
        <span className="rounded-full border border-border/60 bg-veil/[0.03] px-3 py-1 text-[10px] text-muted/80">{m.content}</span>
      </div>
    )
  }

  const copy = () => {
    navigator.clipboard?.writeText(m.content)
    setCopied(true)
    setTimeout(() => setCopied(false), 1200)
  }

  return (
    <motion.div
      layout
      initial={{ opacity: 0, y: 10, scale: 0.98 }}
      animate={{ opacity: 1, y: 0, scale: 1 }}
      transition={{ type: 'spring', stiffness: 420, damping: 32 }}
      className={cn('group flex gap-2.5', isUser ? 'flex-row-reverse' : 'flex-row')}
    >
      {!isUser && (
        <div className="mt-0.5 grid h-9 w-9 shrink-0 place-items-center self-start rounded-full border border-border bg-field text-lg">
          {m.personaName ? (persona?.avatar || '🎭') : '🎭'}
        </div>
      )}

      <div className={cn('min-w-0 max-w-[72%]', isUser && 'flex flex-col items-end')}>
        {showName && !isUser && (
          <p className="mb-1 flex items-center gap-2 text-[10px] text-muted/70">
            <span>{m.personaName || persona?.name || '角色'}</span>
            {m.mock && <span className="tag text-[8.5px]">mock</span>}
            <span className="opacity-0 transition-opacity group-hover:opacity-60">{time}</span>
          </p>
        )}

        {m.kind === 'sticker' ? (
          <div className="relative">
            <motion.div whileHover={{ scale: 1.04 }} className="text-[58px] leading-none">{m.meta || m.content}</motion.div>
            <div className="msg-actions absolute -top-2 right-0 flex gap-1">
              <button onClick={() => onCollect(m.meta || m.content)}
                className={cn('rounded-full border border-border bg-raised p-1.5 shadow-card', collected ? 'text-accent-300' : 'text-muted hover:text-body')}
                aria-label="收藏表情"><Star size={11} className={collected ? 'fill-current' : ''} /></button>
            </div>
          </div>
        ) : m.kind === 'voice' ? (
          <button
            onClick={() => { setPlaying(p => !p); setTimeout(() => setPlaying(false), 2600) }}
            className={cn('flex items-center gap-2.5 rounded-2xl border px-3.5 py-2.5', isUser ? 'bubble-out' : 'bubble-in')}
            aria-label="播放语音"
          >
            <Play size={13} className={cn(isUser ? 'text-bright' : 'text-accent-300', playing && 'animate-pulse')} />
            <span className="flex h-5 items-end gap-[2px]">
              {[5, 9, 14, 8, 16, 11, 6, 13, 9, 4].map((h, i) => (
                <span key={i}
                  className={cn('w-[2px] rounded-full', isUser ? 'bg-bright/70' : 'bg-accent-300/70', playing && 'animate-wave')}
                  style={{ height: h, animationDelay: `${i * 60}ms` }} />
              ))}
            </span>
            <span className="text-[10.5px] text-muted">{m.meta || '3'}"</span>
          </button>
        ) : (
          <div className={cn('relative whitespace-pre-wrap px-3.5 py-2.5 text-[13px] leading-[1.75]',
            isUser ? 'bubble-out text-bright' : 'bubble-in text-body')}>
            {m.content}
          </div>
        )}

        {(m.kind === 'text' || !m.kind) && (
          <div className={cn('msg-actions mt-1 flex gap-2 text-[9.5px] text-muted/60', isUser ? 'justify-end' : '')}>
            <button onClick={copy} className="hover:text-body">{copied ? '已复制' : '复制'}</button>
            <span>{time}</span>
          </div>
        )}
      </div>
    </motion.div>
  )
}

/* ─── Typing dots ─────────────────────────────────────────────────────────── */

export function TypingDots({ avatar }: { avatar: string }) {
  return (
    <div className="flex items-center gap-2.5">
      <div className="grid h-9 w-9 place-items-center rounded-full border border-border bg-field text-lg">{avatar}</div>
      <div className="bubble-in flex items-center gap-1 px-4 py-3">
        {[0, 130, 260].map(d => (
          <span key={d} className="h-1.5 w-1.5 animate-bounce rounded-full bg-accent-300/70" style={{ animationDelay: `${d}ms` }} />
        ))}
      </div>
    </div>
  )
}

/* ─── Group live switch (used by list footer) ─────────────────────────────── */

export function useGroupsLive(initial = true) {
  const [live, setLive] = useState(initial)
  useEffect(() => { api.setGroupsLive(initial).catch(() => {}) }, [])
  const toggle = () => {
    const next = !live
    setLive(next)
    api.setGroupsLive(next).catch(() => {})
  }
  return { live, toggle }
}

/* ─── Date helpers ────────────────────────────────────────────────────────── */

export function dayLabel(iso: string) {
  const d = new Date(iso)
  const now = new Date()
  const sameDay = d.toDateString() === now.toDateString()
  if (sameDay) return '今天'
  const y = new Date(now); y.setDate(now.getDate() - 1)
  if (d.toDateString() === y.toDateString()) return '昨天'
  return d.toLocaleDateString('zh-CN', { month: 'long', day: 'numeric' })
}

export function useNow(interval = 30000) {
  const [now, setNow] = useState(Date.now())
  useEffect(() => {
    const t = setInterval(() => setNow(Date.now()), interval)
    return () => clearInterval(t)
  }, [interval])
  return now
}
