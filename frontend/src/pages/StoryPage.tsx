import { useState, useEffect } from 'react'
import { useNavigate } from 'react-router-dom'
import { Plus, BookOpen, Users, Sparkles } from 'lucide-react'
import { api, type StorySession, type Persona } from '../bridge'
import { Button, EmptyState, PageHeader, cn } from '../components/ui'

export function StoryPage() {
  const nav = useNavigate()
  const [stories, setStories] = useState<StorySession[]>([])
  const [personas, setPersonas] = useState<Persona[]>([])
  const [creating, setCreating] = useState(false)
  const [form, setForm] = useState({ title: '', description: '', userRole: '' })
  const [selectedPersonas, setSelectedPersonas] = useState<string[]>([])

  useEffect(() => {
    api.listStories().then(setStories)
    api.listPersonas().then(setPersonas)
  }, [])

  const togglePersona = (id: string) => {
    setSelectedPersonas(p => p.includes(id) ? p.filter(x => x !== id) : [...p, id])
  }

  const create = async () => {
    if (!form.title.trim() || selectedPersonas.length === 0) return
    const s = await api.createStory(form.title, form.description, selectedPersonas, form.userRole)
    nav(`/story/${s.id}`)
  }

  return (
    <div className="mx-auto max-w-[1120px] px-8 py-7">
      <PageHeader
        title="剧情演绎"
        subtitle="邀请多位角色进入同一场景，你也可以是其中一人。每轮由各角色依次回应。"
        actions={<Button onClick={() => setCreating(v => !v)} icon={<Plus size={14} />}>新剧情</Button>}
      />

      {/* Create form */}
      {creating && (
        <div className="card p-5 mb-6 animate-slide-up space-y-4">
          <h2 className="text-base font-semibold text-bright">创建新剧情</h2>
          <div className="grid grid-cols-2 gap-4">
            <div>
              <label className="label">剧情标题</label>
              <input className="input" placeholder="如：月夜密林中的相遇"
                value={form.title} onChange={e => setForm({ ...form, title: e.target.value })} />
            </div>
            <div>
              <label className="label">你扮演的角色（可选）</label>
              <select className="input" value={form.userRole}
                onChange={e => setForm({ ...form, userRole: e.target.value })}>
                <option value="">— 旁白 / 不扮演 —</option>
                {selectedPersonas.map(pid => {
                  const p = personas.find(x => x.id === pid)
                  return p ? <option key={p.id} value={p.id}>{p.avatar} {p.name}</option> : null
                })}
              </select>
            </div>
          </div>
          <div>
            <label className="label">剧情描述 / 背景设定</label>
            <textarea className="input min-h-[80px]" placeholder="简短描述故事背景、地点、氛围…"
              value={form.description} onChange={e => setForm({ ...form, description: e.target.value })} />
          </div>
          <div>
            <label className="label">邀请角色（至少 1 个）</label>
            <div className="grid grid-cols-3 gap-2 mt-1">
              {personas.length === 0 ? (
                <p className="text-muted text-sm col-span-3">先在「人设」页创建角色</p>
              ) : personas.map(p => {
                const sel = selectedPersonas.includes(p.id)
                return (
                  <button key={p.id} onClick={() => togglePersona(p.id)}
                    className={`flex items-center gap-2 px-3 py-2 rounded-md text-sm transition-all border ${sel ? 'bg-accent-300/12 border-accent-300/40 text-accent-200' : 'bg-surface border-border text-muted hover:border-border/80 hover:text-body'}`}>
                    <span>{p.avatar}</span>
                    <span className="truncate">{p.name}</span>
                    {sel && <span className="ml-auto text-accent-400 text-xs">✓</span>}
                  </button>
                )
              })}
            </div>
          </div>
          <div className="flex justify-end gap-3">
            <button onClick={() => setCreating(false)} className="btn-ghost">取消</button>
            <button onClick={create}
              disabled={!form.title.trim() || selectedPersonas.length === 0}
              className="btn-primary">
              开始演绎
            </button>
          </div>
        </div>
      )}

      {/* Story list */}
      {stories.length === 0 ? (
        <EmptyState
          icon={<BookOpen size={20} />}
          title="还没有剧情"
          description="设定一个场景，邀请两位角色，故事会从第一句话开始。"
          action={<Button size="sm" onClick={() => setCreating(true)} icon={<Plus size={13} />}>新剧情</Button>}
        />
      ) : (
        <div className="grid grid-cols-1 gap-4 md:grid-cols-2">
          {stories.map(s => {
            const chars = (s.personaIds || []).map(pid => personas.find(p => p.id === pid)).filter(Boolean) as Persona[]
            return (
              <div key={s.id} onClick={() => nav(`/story/${s.id}`)}
                className="card card-hover cursor-pointer p-5">
                <div className="mb-2 flex items-start justify-between">
                  <h3 className="display text-[16px]">{s.title}</h3>
                  <span className="text-[10px] text-muted">{(s.messages || []).length} 条消息</span>
                </div>
                <p className="mb-3 line-clamp-2 text-[11.5px] leading-relaxed text-muted">{s.description || '未填写场景描述'}</p>
                <div className="flex items-center gap-1.5 border-t border-border/60 pt-3">
                  <Users size={11} className="text-muted/70" />
                  {chars.map(p => <span key={p.id} className="text-base">{p.avatar}</span>)}
                  <span className="ml-1 text-[10.5px] text-muted">{chars.map(p => p.name).join('、')}</span>
                </div>
              </div>
            )
          })}
        </div>
      )}
    </div>
  )
}
