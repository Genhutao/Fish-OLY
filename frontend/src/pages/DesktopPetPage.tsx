import { useEffect, useMemo, useRef, useState } from 'react'
import { Cat, MessageCircle, Move, Sparkles } from 'lucide-react'
import { api } from '../bridge'
import { usePersonas } from '../hooks/useData'
import { PageHeader, Segmented, Switch, cn, useToast } from '../components/ui'

interface Position { x: number; y: number }
interface PetMessage { id: number; text: string }

const IDLE_LINES = [
  '…好像有什么东西飘过去了。',
  '*打了个哈欠*',
  '……（望着某处）',
  '嗯？你在看我？',
  '有点困了呢。',
  '今天的月亮很圆。',
]

export function DesktopPetPage() {
  const toast = useToast()
  const { personas } = usePersonas()
  const [selectedId, setSelectedId] = useState<string>('')
  const [pos, setPos] = useState<Position>({ x: 60, y: 60 })
  const [dragging, setDragging] = useState(false)
  const [dragOffset, setDragOffset] = useState<Position>({ x: 0, y: 0 })
  const [messages, setMessages] = useState<PetMessage[]>([])
  const [inputText, setInputText] = useState('')
  const [chatting, setChatting] = useState(false)
  const [showChat, setShowChat] = useState(false)
  const [bounce, setBounce] = useState(false)
  const [idleSec, setIdleSec] = useState('8')
  const [size, setSize] = useState(64)
  const [chatter, setChatter] = useState(true)
  const msgId = useRef(0)
  const containerRef = useRef<HTMLDivElement>(null)

  const persona = personas.find(p => p.id === selectedId) ?? (personas[0] || null)

  const stars = useMemo(
    () => Array.from({ length: 22 }, (_, i) => ({
      left: (i * 37 + 13) % 100,
      top: (i * 53 + 7) % 100,
      delay: (i % 7) * 0.4,
    })),
    [],
  )

  useEffect(() => {
    if (!persona || !chatter) return
    const t = setInterval(() => {
      if (!chatting && Math.random() > 0.55) {
        const line = IDLE_LINES[Math.floor(Math.random() * IDLE_LINES.length)]
        addBubble(line)
      }
    }, Number(idleSec) * 1000)
    return () => clearInterval(t)
  }, [persona, chatting, chatter, idleSec])

  const addBubble = (text: string) => {
    const id = ++msgId.current
    setMessages(prev => [...prev.slice(-3), { id, text }])
    setTimeout(() => setMessages(prev => prev.filter(m => m.id !== id)), 6000)
    setBounce(true)
    setTimeout(() => setBounce(false), 400)
  }

  const chat = async () => {
    if (!persona || !inputText.trim()) return
    const q = inputText.trim(); setInputText(''); setChatting(true)
    try {
      const r = await api.chat({ personaId: persona.id, message: q })
      addBubble(r.content)
    } catch { addBubble('……') }
    finally { setChatting(false) }
  }

  const onMouseDown = (e: React.MouseEvent) => {
    setDragging(true)
    setDragOffset({ x: e.clientX - pos.x, y: e.clientY - pos.y })
  }

  useEffect(() => {
    if (!dragging) return
    const move = (e: MouseEvent) => {
      const container = containerRef.current
      if (!container) return
      const rect = container.getBoundingClientRect()
      setPos({
        x: Math.max(0, Math.min(rect.width - size - 8, e.clientX - rect.left - dragOffset.x)),
        y: Math.max(0, Math.min(rect.height - size - 24, e.clientY - rect.top - dragOffset.y)),
      })
    }
    const up = () => setDragging(false)
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
    return () => { window.removeEventListener('mousemove', move); window.removeEventListener('mouseup', up) }
  }, [dragging, dragOffset, size])

  return (
    <div className="mx-auto max-w-[1040px] px-8 py-7">
      <PageHeader
        title="卓宠"
        subtitle="把角色放到一块小小的夜色里。她会自己说话，也会记住你对她说过的话。"
        actions={
          <label className="flex items-center gap-2 text-[11px] text-muted">
            闲聊间隔
            <select className="input h-8 w-[92px] text-[11px]" value={idleSec} onChange={e => setIdleSec(e.target.value)} aria-label="闲聊间隔">
              <option value="6">6 秒</option>
              <option value="8">8 秒</option>
              <option value="16">16 秒</option>
            </select>
          </label>
        }
      />

      <div className="mb-4 flex flex-wrap items-center gap-4">
        <select className="input w-52" value={selectedId || persona?.id || ''}
          onChange={e => setSelectedId(e.target.value)} aria-label="选择角色">
          {personas.map(p => <option key={p.id} value={p.id}>{p.avatar} {p.name}</option>)}
        </select>
        <label className="flex items-center gap-2 text-[11px] text-muted">
          体型
          <input type="range" min={48} max={96} step={4} value={size} onChange={e => setSize(Number(e.target.value))}
            className="range w-24" style={{ ['--fill' as any]: `${((size - 48) / 48) * 100}%` }} aria-label="角色大小" />
        </label>
        <label className="flex items-center gap-2 text-[11px] text-muted">
          自动闲聊
          <Switch checked={chatter} onChange={setChatter} label="自动闲聊" />
        </label>
        <span className="ml-auto text-[10.5px] text-muted/60">拖动角色改变位置 · 点击打开对话</span>
      </div>

      <div
        ref={containerRef}
        className="card relative overflow-hidden"
        style={{
          height: 470,
          background:
            'radial-gradient(70% 60% at 24% 78%, rgba(231,198,125,0.08), transparent 70%), radial-gradient(60% 50% at 82% 18%, rgba(121,180,168,0.06), transparent 70%)',
        }}
      >
        {stars.map((s, i) => (
          <span key={i} className="absolute h-[2px] w-[2px] rounded-full bg-accent-100/40 animate-pulse-soft"
            style={{ left: `${s.left}%`, top: `${s.top}%`, animationDelay: `${s.delay}s` }} />
        ))}

        {!persona ? (
          <div className="absolute inset-0 grid place-items-center text-[12px] text-muted">先在「人设」页创建角色</div>
        ) : (
          <>
            <div className="pointer-events-none absolute left-0 right-0 top-5 flex flex-col items-center gap-2">
              {messages.map(m => (
                <div key={m.id} className="animate-slide-up max-w-xs rounded-lg border border-border bg-raised/95 px-3.5 py-2 text-center text-[12px] leading-relaxed text-body shadow-card">
                  {m.text}
                </div>
              ))}
            </div>

            {persona && (
              <div
                style={{ position: 'absolute', left: pos.x, top: pos.y, cursor: dragging ? 'grabbing' : 'grab' }}
                onMouseDown={onMouseDown}
                onClick={() => { if (!dragging) setShowChat(v => !v) }}
                className={cn('select-none transition-transform duration-300', bounce ? 'scale-110' : 'scale-100')}
              >
                <div
                  className="grid place-items-center rounded-[18px] border border-accent-300/25 bg-field shadow-glow"
                  style={{ width: size, height: size, fontSize: size * 0.55 }}
                >
                  {persona.avatar}
                </div>
                <div className="mt-1.5 text-center text-[10px] text-muted">{persona.name}</div>
              </div>
            )}

            {showChat && (
              <div className="absolute bottom-4 left-1/2 w-[300px] -translate-x-1/2 animate-slide-up rounded-lg border border-border bg-raised/95 p-3 shadow-modal backdrop-blur">
                <div className="mb-2 flex items-center gap-1.5 text-[11px] text-muted">
                  <MessageCircle size={11} /> {persona.avatar} {persona.name}
                </div>
                <div className="flex gap-2">
                  <input className="input h-8 flex-1 text-[12px]" placeholder="说点什么…"
                    value={inputText} onChange={e => setInputText(e.target.value)}
                    onKeyDown={e => { if (e.key === 'Enter') chat() }}
                    disabled={chatting} autoFocus aria-label="卓宠对话输入" />
                  <button onClick={chat} disabled={chatting || !inputText.trim()} className="btn-primary btn-sm h-8">
                    {chatting ? '…' : '说'}
                  </button>
                </div>
              </div>
            )}
          </>
        )}
      </div>

      <div className="mt-4 grid grid-cols-1 gap-3 sm:grid-cols-3">
        {[
          { icon: Move, title: '拖动', text: '按住角色拖动，位置会保留在当前页面。' },
          { icon: Sparkles, title: '闲聊', text: '空闲时随机说一句；对话会写入记忆库。' },
          { icon: Cat, title: '正式版', text: '可接入系统桌面层，做成真正的透明悬浮窗。' },
        ].map(({ icon: Icon, title, text }) => (
          <div key={title} className="card flex items-start gap-2.5 p-3.5">
            <Icon size={14} className="mt-0.5 text-accent-300" />
            <div>
              <p className="text-[11.5px] text-body">{title}</p>
              <p className="mt-0.5 text-[10.5px] leading-relaxed text-muted">{text}</p>
            </div>
          </div>
        ))}
      </div>
    </div>
  )
}
