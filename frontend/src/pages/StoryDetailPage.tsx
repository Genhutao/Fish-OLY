import { useState, useEffect, useRef } from 'react'
import { useParams, useNavigate } from 'react-router-dom'
import { ArrowLeft, Send, Users } from 'lucide-react'
import { api, type StorySession, type Persona } from '../bridge'

export function StoryDetailPage() {
  const { id } = useParams<{ id: string }>()
  const nav = useNavigate()
  const [story, setStory] = useState<StorySession | null>(null)
  const [personas, setPersonas] = useState<Persona[]>([])
  const [input, setInput] = useState('')
  const [sending, setSending] = useState(false)
  const scrollRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!id) return
    api.getStory(id).then(setStory)
    api.listPersonas().then(setPersonas)
  }, [id])

  useEffect(() => {
    scrollRef.current?.scrollTo({ top: scrollRef.current.scrollHeight, behavior: 'smooth' })
  }, [story?.messages])

  const send = async () => {
    if (!input.trim() || !story || sending) return
    const text = input.trim(); setInput(''); setSending(true)
    try {
      const updated = await api.sendStoryMessage(story.id, story.userRole || '', text)
      setStory(updated)
    } catch (e) { console.error(e) }
    finally { setSending(false) }
  }

  const getPersona = (pid: string) => personas.find(p => p.id === pid)

  if (!story) return <div className="p-6 text-muted">加载中…</div>
  const chars = (story.personaIds || []).map(pid => getPersona(pid)).filter(Boolean) as Persona[]

  return (
    <div className="flex flex-col h-full animate-fade-in">
      <div className="border-b border-border/70 bg-panel/50 px-6 py-3.5">
        <div className="mx-auto flex max-w-[760px] items-center gap-3">
          <button onClick={() => nav('/story')} className="btn-icon -ml-1" aria-label="返回剧情列表"><ArrowLeft size={15} /></button>
          <div className="min-w-0 flex-1">
            <h1 className="display truncate text-[17px]">{story.title}</h1>
            <div className="mt-0.5 flex items-center gap-2">
              {chars.map(p => (
                <span key={p.id} title={p.name} className="text-[13px]">{p.avatar}</span>
              ))}
              <span className="text-[10px] text-muted">{(story.messages || []).length} 条消息</span>
              <span className="text-[10px] text-muted/50">·</span>
              <span className="text-[10px] text-muted">{chars.map(p => p.name).join('、')}</span>
            </div>
          </div>
          <span className="tag">多人演绎</span>
        </div>
        {story.description && (
          <div className="mx-auto mt-2 max-w-[760px]">
            <p className="quote text-[11px] leading-relaxed text-muted/80">{story.description}</p>
          </div>
        )}
      </div>

      <div ref={scrollRef} className="flex-1 overflow-y-auto px-6 py-5">
        <div className="mx-auto max-w-[760px] space-y-4">
          {(story.messages || []).length === 0 && (
            <div className="py-16 text-center">
              <div className="mb-3 flex justify-center gap-2 text-3xl">
                {chars.map(p => <span key={p.id} className="animate-float">{p.avatar}</span>)}
              </div>
              <p className="text-[12px] text-muted">发送第一条消息，故事就此开始…</p>
            </div>
          )}
          {(story.messages || []).map(m => {
            const p = m.personaId ? getPersona(m.personaId) : null
            const isUser = m.role === 'user'
            return (
              <div key={m.id} className={`flex ${isUser ? 'justify-end' : 'justify-start'} animate-slide-up`}>
                <div className={`flex max-w-[78%] gap-2.5 ${isUser ? 'flex-row-reverse' : ''}`}>
                  <div className="mt-0.5 grid h-7 w-7 shrink-0 place-items-center rounded-full border border-border bg-field text-[15px]">
                    {p ? p.avatar : (isUser ? '🧑' : '📖')}
                  </div>
                  <div>
                    <div className={`mb-1 text-[10px] text-muted/70 ${isUser ? 'text-right' : ''}`}>
                      {m.personaName || (isUser ? '旁白' : '？')}
                    </div>
                    <div className={`whitespace-pre-wrap rounded-lg border px-3.5 py-2.5 text-[12.5px] leading-[1.8] ${
                      isUser
                        ? 'border-accent-300/30 bg-accent-300/[0.09] text-bright'
                        : 'border-border bg-raised/90 text-body'
                    }`}>{m.content}</div>
                  </div>
                </div>
              </div>
            )
          })}
          {sending && (
            <div className="flex animate-fade-in justify-start">
              <div className="rounded-lg border border-border bg-raised/90 px-4 py-3">
                <div className="flex gap-1">
                  {[0,120,240].map(d => (
                    <span key={d} className="h-1.5 w-1.5 animate-bounce rounded-full bg-accent-300/70"
                      style={{ animationDelay: `${d}ms` }} />
                  ))}
                </div>
              </div>
            </div>
          )}
        </div>
      </div>

      <div className="border-t border-border/70 bg-panel/50 px-6 py-4">
        <div className="mx-auto max-w-[760px]">
          {story.userRole && (
            <div className="mb-2 text-[10.5px] text-muted">
              你正在扮演：{getPersona(story.userRole)?.avatar} {getPersona(story.userRole)?.name}
            </div>
          )}
          <div className="flex gap-2">
            <input
              className="input flex-1"
              placeholder={story.userRole ? `${getPersona(story.userRole)?.name || '角色'} 说…` : '旁白 / 叙述…'}
              value={input}
              onChange={e => setInput(e.target.value)}
              onKeyDown={e => { if (e.key === 'Enter' && !e.shiftKey) { e.preventDefault(); send() } }}
              disabled={sending}
              aria-label="剧情输入"
            />
            <button onClick={send} disabled={sending || !input.trim()} className="btn-primary px-4" aria-label="发送">
              <Send size={15} />
            </button>
          </div>
        </div>
      </div>
    </div>
  )
}
