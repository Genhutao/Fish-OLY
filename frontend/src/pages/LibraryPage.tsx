import { useEffect, useMemo, useRef, useState } from 'react'
import { Link } from 'react-router-dom'
import {
  Check, Download, Eye, Github, GripVertical, Library, PackageCheck, Plus,
  Search, Sparkles, Star, Trash2, Wand2,
} from 'lucide-react'
import { api, type GitHubSkill, type Persona, type Preset, type Skill } from '../bridge'
import {
  Button, EmptyState, Field, IconButton, Modal, PageHeader, Segmented, cn, useToast,
} from '../components/ui'

const emptySkill = { name: '', icon: '✨', category: '', description: '', prompt: '' }
const emptyPreset = { name: '', icon: '🎭', description: '', scenario: '', style: '', systemSnippet: '', tags: '' }

type Tab = 'skills' | 'presets' | 'github'

export function LibraryPage() {
  const toast = useToast()
  const [tab, setTab] = useState<Tab>('skills')
  const [skills, setSkills] = useState<Skill[]>([])
  const [presets, setPresets] = useState<Preset[]>([])
  const [personas, setPersonas] = useState<Persona[]>([])
  const [query, setQuery] = useState('')
  const [category, setCategory] = useState('')
  const [createOpen, setCreateOpen] = useState(false)
  const [saving, setSaving] = useState(false)
  const [skillForm, setSkillForm] = useState(emptySkill)
  const [presetForm, setPresetForm] = useState(emptyPreset)
  const [target, setTarget] = useState<Record<string, string>>({})
  const [preview, setPreview] = useState<{ open: boolean; text: string; who: string }>({ open: false, text: '', who: '' })
  const [deleting, setDeleting] = useState<{ kind: 'skill' | 'preset'; item: Skill | Preset } | null>(null)

  const [ghQuery, setGhQuery] = useState('')
  const [ghResults, setGhResults] = useState<GitHubSkill[]>([])
  const [ghBusy, setGhBusy] = useState(false)
  const [importing, setImporting] = useState('')

  const [drag, setDrag] = useState<{ skill: Skill; x: number; y: number; over: string } | null>(null)
  const personaRefs = useRef<Record<string, HTMLDivElement | null>>({})

  const load = () => {
    api.listSkills().then(list => setSkills(list || [])).catch(console.error)
    api.listPresets().then(list => setPresets(list || [])).catch(console.error)
    api.listPersonas().then(list => setPersonas(list || [])).catch(console.error)
  }
  useEffect(load, [])

  const equipCount = useMemo(() => {
    const map: Record<string, number> = {}
    personas.forEach(p => (p.skillIds || []).forEach(id => { map[id] = (map[id] || 0) + 1 }))
    return map
  }, [personas])

  const categories = useMemo(() => Array.from(new Set(skills.map(s => s.category))), [skills])

  const filteredSkills = useMemo(() => {
    const q = query.trim().toLowerCase()
    return skills.filter(s =>
      (!q || (s.name + s.description + s.prompt).toLowerCase().includes(q)) &&
      (!category || s.category === category))
  }, [skills, query, category])

  const filteredPresets = useMemo(() => {
    const q = query.trim().toLowerCase()
    return presets.filter(p => !q || (p.name + p.description + p.style + p.scenario).toLowerCase().includes(q))
  }, [presets, query])

  const createSkill = async () => {
    if (!skillForm.name.trim() || !skillForm.prompt.trim()) return
    setSaving(true)
    try {
      await api.createSkill({ ...skillForm, category: skillForm.category || '自定义' })
      setCreateOpen(false); setSkillForm(emptySkill); load(); toast.push('技能已创建')
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
    finally { setSaving(false) }
  }

  const createPreset = async () => {
    if (!presetForm.name.trim() || (!presetForm.style.trim() && !presetForm.systemSnippet.trim())) return
    setSaving(true)
    try {
      await api.createPreset({ ...presetForm, tags: presetForm.tags.split(/[,，\s]+/).filter(Boolean) })
      setCreateOpen(false); setPresetForm(emptyPreset); load(); toast.push('预设已创建')
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
    finally { setSaving(false) }
  }

  const confirmDelete = async () => {
    if (!deleting) return
    try {
      if (deleting.kind === 'skill') await api.deleteSkill(deleting.item.id)
      else await api.deletePreset(deleting.item.id)
      setDeleting(null); load(); toast.push('已删除')
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
  }

  const applyPreset = async (preset: Preset) => {
    const pid = target[preset.id] || personas[0]?.id
    if (!pid) return
    try { await api.applyPreset(pid, preset.id); load(); toast.push('预设已应用到人设') }
    catch (e: any) { toast.push(String(e?.message || e), 'error') }
  }

  const toggleSkill = async (skill: Skill, personaId: string) => {
    if (!personaId) return
    try { await api.toggleSkill(personaId, skill.id); load() } catch (e: any) { toast.push(String(e?.message || e), 'error') }
  }

  const showPrompt = async (personaId: string) => {
    if (!personaId) return
    try {
      const text = await api.previewPersonaPrompt(personaId)
      setPreview({ open: true, text, who: personas.find(p => p.id === personaId)?.name || '' })
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
  }

  const searchGitHub = async () => {
    setGhBusy(true)
    try { setGhResults(await api.searchGitHubSkills(ghQuery)) }
    catch (e: any) { toast.push(String(e?.message || e), 'error') }
    finally { setGhBusy(false) }
  }

  const importSkill = async (r: GitHubSkill) => {
    setImporting(r.fullName)
    try {
      const s = await api.importGitHubSkill(r.fullName)
      load()
      toast.push(`已导入「${s.name}」，可在技能页拖给角色`)
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
    finally { setImporting('') }
  }

  const hitPersona = (x: number, y: number) => {
    for (const [pid, el] of Object.entries(personaRefs.current)) {
      if (!el) continue
      const r = el.getBoundingClientRect()
      if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom) return pid
    }
    return ''
  }

  const startDrag = (e: React.PointerEvent, skill: Skill) => {
    e.preventDefault()
    setDrag({ skill, x: e.clientX, y: e.clientY, over: '' })
    const move = (ev: PointerEvent) => {
      setDrag(d => d ? { ...d, x: ev.clientX, y: ev.clientY, over: hitPersona(ev.clientX, ev.clientY) } : d)
    }
    const up = (ev: PointerEvent) => {
      const pid = hitPersona(ev.clientX, ev.clientY)
      setDrag(null)
      window.removeEventListener('pointermove', move)
      window.removeEventListener('pointerup', up)
      if (!pid) return
      const persona = personas.find(p => p.id === pid)
      if (!persona) return
      if ((persona.skillIds || []).includes(skill.id)) {
        toast.push(`${persona.name} 已经装备了这个技能`, 'info'); return
      }
      api.toggleSkill(pid, skill.id).then(() => {
        load()
        toast.push(`已把「${skill.name}」拖给 ${persona.name}`)
      }).catch((err: any) => toast.push(String(err?.message || err), 'error'))
    }
    window.addEventListener('pointermove', move)
    window.addEventListener('pointerup', up)
  }

  const openCreate = () => {
    if (tab === 'skills') setSkillForm(emptySkill)
    else if (tab === 'presets') setPresetForm(emptyPreset)
    setCreateOpen(true)
  }

  return (
    <div className="mx-auto max-w-[1160px] px-8 py-7">
      <PageHeader
        title="技能库"
        subtitle="技能是能力提示，预设是场景与文风。可以从 GitHub 搜一个技能，直接拖到角色身上装配。"
        actions={
          <>
            {tab !== 'github' && <Link to="/settings"><Button variant="ghost" icon={<Github size={14} />}>GitHub Token</Button></Link>}
            {tab !== 'github' && <Button onClick={openCreate} icon={<Plus size={15} />}>新建{tab === 'skills' ? '技能' : '预设'}</Button>}
          </>
        }
      />

      <div className="mb-5 flex flex-wrap items-center gap-2">
        <Segmented
          value={tab}
          onChange={v => { setTab(v); setCategory('') }}
          options={[
            { value: 'skills', label: `技能 ${skills.length}` },
            { value: 'presets', label: `预设 ${presets.length}` },
            { value: 'github', label: <span className="flex items-center gap-1"><Github size={11} /> GitHub</span> },
          ]}
        />
        {tab !== 'github' && (
          <>
            <div className="relative ml-1 w-[220px]">
              <Search size={13} className="pointer-events-none absolute left-3 top-1/2 -translate-y-1/2 text-muted" />
              <input className="input pl-8" placeholder="搜索名称或内容" value={query} onChange={e => setQuery(e.target.value)} aria-label="搜索技能或预设" />
            </div>
            {tab === 'skills' && (
              <div className="flex flex-1 flex-wrap gap-1.5">
                <button onClick={() => setCategory('')}
                  className={cn('h-7 rounded-full border px-2.5 text-[11px] transition-colors', !category ? 'border-accent-300/40 bg-accent-300/10 text-accent-200' : 'border-border text-muted hover:text-body')}>
                  全部
                </button>
                {categories.map(c => (
                  <button key={c} onClick={() => setCategory(category === c ? '' : c)}
                    className={cn('h-7 rounded-full border px-2.5 text-[11px] transition-colors', category === c ? 'border-accent-300/40 bg-accent-300/10 text-accent-200' : 'border-border text-muted hover:text-body')}>
                    {c}
                  </button>
                ))}
              </div>
            )}
          </>
        )}
      </div>

      {tab === 'skills' && (
        filteredSkills.length === 0 ? (
          <EmptyState icon={<Library size={20} />} title="没有匹配的技能" description="换个关键词，或清空分类筛选。" />
        ) : (
          <div className="grid grid-cols-1 gap-3 md:grid-cols-2 lg:grid-cols-3">
            {filteredSkills.map(s => (
              <article key={s.id} className={cn('card card-hover group flex flex-col p-4', drag?.skill.id === s.id && 'opacity-40')}>
                <div className="flex items-start gap-2.5">
                  <button
                    onPointerDown={e => startDrag(e, s)}
                    className="mt-1 cursor-grab touch-none rounded p-0.5 text-muted/50 opacity-0 transition-opacity hover:text-accent-200 group-hover:opacity-100 active:cursor-grabbing"
                    aria-label={`拖动 ${s.name} 到角色`}
                    title="拖到下方角色上装配"
                  >
                    <GripVertical size={14} />
                  </button>
                  <div className="grid h-9 w-9 place-items-center rounded-md border border-border bg-veil/[0.04] text-lg">{s.icon}</div>
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center gap-1.5">
                      <h3 className="truncate text-[13.5px] font-medium text-bright">{s.name}</h3>
                      {s.builtin && <span className="tag text-[9.5px]">内置</span>}
                      {s.source && <a href={s.source} target="_blank" rel="noreferrer" className="text-[9.5px] text-aurora hover:underline">GitHub</a>}
                    </div>
                    <p className="mt-0.5 text-[10.5px] text-muted">{s.category}</p>
                  </div>
                  {!s.builtin && (
                    <IconButton label="删除技能" onClick={() => setDeleting({ kind: 'skill', item: s })} className="h-7 w-7 hover:text-ember">
                      <Trash2 size={13} />
                    </IconButton>
                  )}
                </div>
                <p className="mt-3 text-[11.5px] leading-relaxed text-body/70">{s.description}</p>
                <p className="mt-2 line-clamp-3 rounded-md border border-border/60 bg-field px-2.5 py-2 font-mono text-[10px] leading-relaxed text-muted">{s.prompt}</p>
                <div className="mt-auto pt-3">
                  <div className="mb-2 flex items-center gap-1.5 text-[10.5px] text-muted">
                    <PackageCheck size={12} className="text-aurora" /> 已装备 {equipCount[s.id] || 0} 人
                  </div>
                  <select className="input h-8 text-[11px]" value="" aria-label={`装备${s.name}`}
                    onChange={e => toggleSkill(s, e.target.value)}>
                    <option value="">装备到…</option>
                    {personas.map(p => (
                      <option key={p.id} value={p.id}>
                        {p.avatar} {p.name} {(p.skillIds || []).includes(s.id) ? '（已装备）' : ''}
                      </option>
                    ))}
                  </select>
                </div>
              </article>
            ))}
          </div>
        )
      )}

      {tab === 'presets' && (
        filteredPresets.length === 0 ? (
          <EmptyState icon={<Wand2 size={20} />} title="没有匹配的预设" description="换个关键词试试。" />
        ) : (
          <div className="grid grid-cols-1 gap-4 md:grid-cols-2">
            {filteredPresets.map(p => {
              const users = personas.filter(x => x.presetId === p.id)
              return (
                <article key={p.id} className="card card-hover flex flex-col p-5">
                  <div className="flex items-start gap-3">
                    <span className="text-xl">{p.icon}</span>
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center gap-2">
                        <h3 className="display text-[15px]">{p.name}</h3>
                        {p.builtin && <span className="tag text-[9.5px]">内置</span>}
                      </div>
                      <p className="mt-0.5 text-[11px] text-muted">{p.description}</p>
                    </div>
                    {!p.builtin && (
                      <IconButton label="删除预设" onClick={() => setDeleting({ kind: 'preset', item: p })} className="h-7 w-7 hover:text-ember">
                        <Trash2 size={13} />
                      </IconButton>
                    )}
                  </div>
                  <div className="mt-3 space-y-1.5 text-[11px]">
                    {p.scenario && <p className="text-body/70"><span className="text-muted">场景 · </span>{p.scenario}</p>}
                    {p.style && <p className="text-body/70"><span className="text-muted">文风 · </span>{p.style}</p>}
                    {p.systemSnippet && <p className="line-clamp-2 rounded-md border border-border/60 bg-field px-2.5 py-2 leading-relaxed text-muted">{p.systemSnippet}</p>}
                  </div>
                  {(p.tags || []).length > 0 && (
                    <div className="mt-3 flex flex-wrap gap-1.5">
                      {(p.tags || []).map((t, i) => <span key={i} className="tag text-[9.5px]">{t}</span>)}
                    </div>
                  )}
                  <div className="mt-auto pt-3">
                    <p className="mb-2 flex items-center gap-1.5 text-[10.5px] text-muted">
                      <Check size={11} className={users.length ? 'text-aurora' : 'text-muted/40'} />
                      {users.length ? `使用中：${users.map(u => u.name).join('、')}` : '尚未被任何角色使用'}
                    </p>
                    <div className="flex gap-1.5">
                      <select className="input h-8 flex-1 text-[11px]"
                        value={target[p.id] || ''}
                        onChange={e => setTarget(prev => ({ ...prev, [p.id]: e.target.value }))}
                        aria-label={`选择角色应用${p.name}`}>
                        <option value="">选择角色…</option>
                        {personas.map(x => (
                          <option key={x.id} value={x.id}>{x.avatar} {x.name}{x.presetId === p.id ? '（当前）' : ''}</option>
                        ))}
                      </select>
                      <Button size="sm" className="h-8" onClick={() => applyPreset(p)} disabled={!target[p.id]} icon={<Wand2 size={12} />}>应用</Button>
                      <Button variant="outline" size="sm" className="h-8 px-2" onClick={() => showPrompt(target[p.id] || users[0]?.id || personas[0]?.id || '')} icon={<Eye size={12} />} aria-label="预览提示词" />
                    </div>
                  </div>
                </article>
              )
            })}
          </div>
        )
      )}

      {tab === 'github' && (
        <div className="space-y-4">
          <div className="card p-4">
            <div className="flex flex-wrap items-center gap-2">
              <div className="relative min-w-[240px] flex-1">
                <Github size={14} className="pointer-events-none absolute left-3 top-1/2 -translate-y-1/2 text-muted" />
                <input className="input pl-8" placeholder="搜索 GitHub 上的 agent / UI skill，例如 frontend design"
                  value={ghQuery} onChange={e => setGhQuery(e.target.value)}
                  onKeyDown={e => { if (e.key === 'Enter') searchGitHub() }}
                  aria-label="搜索 GitHub 技能" />
              </div>
              <Button onClick={searchGitHub} loading={ghBusy} icon={<Search size={13} />}>搜索</Button>
            </div>
            <p className="hint mt-2">
              通过 GitHub 仓库搜索查找带 <code className="font-mono text-[10px] text-accent-200">SKILL.md</code> 的项目；导入后成为本地技能，可拖给角色。
              未配置 Token 时受 API 限流影响。
            </p>
          </div>

          {ghResults.length === 0 ? (
            <EmptyState icon={<Github size={20} />} title="搜索 GitHub 技能" description="输入关键词，例如 frontend design、micro interactions、design system。也可以直接搜索仓库名。" />
          ) : (
            <div className="grid grid-cols-1 gap-3 md:grid-cols-2">
              {ghResults.map(r => (
                <article key={r.fullName} className="card card-hover flex flex-col p-4">
                  <div className="flex items-start gap-2.5">
                    <div className="grid h-9 w-9 place-items-center rounded-md border border-border bg-field text-lg">🐙</div>
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center gap-2">
                        <h3 className="truncate text-[12.5px] text-bright">{r.fullName}</h3>
                        <span className="ml-auto flex items-center gap-1 font-mono text-[10px] text-muted">
                          <Star size={10} className="text-accent-300" />{r.stars}
                        </span>
                      </div>
                      <p className="mt-1 line-clamp-2 text-[11px] leading-relaxed text-muted">{r.description || '（无描述）'}</p>
                    </div>
                  </div>
                  <div className="mt-3 flex items-center gap-2">
                    <a href={r.url} target="_blank" rel="noreferrer" className="btn-ghost h-8 px-2 text-[11px]">打开仓库</a>
                    <Button size="sm" className="ml-auto h-8" loading={importing === r.fullName}
                      onClick={() => importSkill(r)} icon={<Download size={12} />}>导入技能</Button>
                  </div>
                </article>
              ))}
            </div>
          )}
        </div>
      )}

      {/* Drag dock */}
      <div className="sticky bottom-0 z-20 mt-6 -mx-2 rounded-xl border border-border bg-raised/92 px-3 py-2.5 shadow-card backdrop-blur">
        <div className="flex flex-wrap items-center gap-2">
          <span className={cn('flex items-center gap-1.5 text-[10.5px] transition-colors', drag ? 'text-accent-200' : 'text-muted')}>
            <GripVertical size={12} /> {drag ? '松手装配到角色' : '拖动技能卡左侧手柄，拖到角色上装配'}
          </span>
          <div className="ml-auto flex flex-wrap gap-1.5">
            {personas.map(p => {
              const hot = drag?.over === p.id
              return (
                <div
                  key={p.id}
                  ref={el => { personaRefs.current[p.id] = el }}
                  className={cn('flex items-center gap-1.5 rounded-full border px-2.5 py-1 transition-all',
                    hot ? 'scale-105 border-accent-300/70 bg-accent-300/15 shadow-glow' : 'border-border bg-field')}
                >
                  <span className="text-sm">{p.avatar}</span>
                  <span className="text-[10.5px] text-body/85">{p.name}</span>
                  <span className="font-mono text-[9px] text-muted">{(p.skillIds || []).length}</span>
                </div>
              )
            })}
          </div>
        </div>
      </div>

      {/* Drag ghost */}
      {drag && (
        <div className="pointer-events-none fixed z-[80] -translate-x-1/2 -translate-y-1/2"
          style={{ left: drag.x, top: drag.y }}>
          <div className="flex w-[220px] items-center gap-2 rounded-xl border border-accent-300/50 bg-raised px-3 py-2.5 shadow-modal"
            style={{ transform: 'rotate(-2deg) scale(1.02)' }}>
            <span className="text-lg">{drag.skill.icon}</span>
            <div className="min-w-0">
              <p className="truncate text-[11.5px] text-bright">{drag.skill.name}</p>
              <p className="text-[9.5px] text-accent-200">拖到角色上装配</p>
            </div>
          </div>
        </div>
      )}

      {/* Create modal */}
      <Modal
        open={createOpen}
        onClose={() => setCreateOpen(false)}
        title={tab === 'skills' ? '新建技能' : '新建预设'}
        description={tab === 'skills' ? '技能是一句可装备的能力提示。' : '预设决定场景、文风与表演要求。'}
        footer={
          <>
            <Button variant="ghost" onClick={() => setCreateOpen(false)}>取消</Button>
            {tab === 'skills'
              ? <Button onClick={createSkill} loading={saving} disabled={!skillForm.name.trim() || !skillForm.prompt.trim()}>保存技能</Button>
              : <Button onClick={createPreset} loading={saving} disabled={!presetForm.name.trim() || (!presetForm.style.trim() && !presetForm.systemSnippet.trim())}>保存预设</Button>}
          </>
        }
      >
        {tab === 'skills' ? (
          <div className="space-y-3">
            <div className="grid grid-cols-[72px_1fr_140px] gap-3">
              <Field label="图标"><input className="input text-center" value={skillForm.icon} onChange={e => setSkillForm({ ...skillForm, icon: e.target.value })} /></Field>
              <Field label="名称"><input className="input" placeholder="占星 · 观星术" value={skillForm.name} onChange={e => setSkillForm({ ...skillForm, name: e.target.value })} /></Field>
              <Field label="分类"><input className="input" placeholder="才艺 / 战斗" value={skillForm.category} onChange={e => setSkillForm({ ...skillForm, category: e.target.value })} /></Field>
            </div>
            <Field label="一句话说明"><input className="input" placeholder="这个技能让角色多会一点什么" value={skillForm.description} onChange={e => setSkillForm({ ...skillForm, description: e.target.value })} /></Field>
            <Field label="提示词" hint="会作为一行进入系统提示词的「已装备技能」段。">
              <textarea className="input min-h-[84px]" placeholder="描写星象时使用真实星座名，并让预言始终暧昧。" value={skillForm.prompt} onChange={e => setSkillForm({ ...skillForm, prompt: e.target.value })} />
            </Field>
          </div>
        ) : (
          <div className="space-y-3">
            <div className="grid grid-cols-[72px_1fr] gap-3">
              <Field label="图标"><input className="input text-center" value={presetForm.icon} onChange={e => setPresetForm({ ...presetForm, icon: e.target.value })} /></Field>
              <Field label="名称"><input className="input" placeholder="黄昏车站" value={presetForm.name} onChange={e => setPresetForm({ ...presetForm, name: e.target.value })} /></Field>
            </div>
            <Field label="说明"><input className="input" placeholder="适合什么场景" value={presetForm.description} onChange={e => setPresetForm({ ...presetForm, description: e.target.value })} /></Field>
            <div className="grid grid-cols-2 gap-3">
              <Field label="场景"><input className="input" placeholder="黄昏的站台，末班车还有十分钟" value={presetForm.scenario} onChange={e => setPresetForm({ ...presetForm, scenario: e.target.value })} /></Field>
              <Field label="文风"><input className="input" placeholder="克制、留白、少形容词" value={presetForm.style} onChange={e => setPresetForm({ ...presetForm, style: e.target.value })} /></Field>
            </div>
            <Field label="表演要求"><textarea className="input min-h-[72px]" placeholder="不要直接说道别，用动作与停顿表达不舍。" value={presetForm.systemSnippet} onChange={e => setPresetForm({ ...presetForm, systemSnippet: e.target.value })} /></Field>
            <Field label="标签"><input className="input" placeholder="现代, 黄昏, 告别" value={presetForm.tags} onChange={e => setPresetForm({ ...presetForm, tags: e.target.value })} /></Field>
          </div>
        )}
      </Modal>

      <Modal open={preview.open} onClose={() => setPreview({ open: false, text: '', who: '' })}
        title={`${preview.who} 的装配结果`} description="真实请求会把记忆段替换为相似度检索到的内容。"
        width="max-w-2xl">
        <pre className="whitespace-pre-wrap break-words font-mono text-[11px] leading-relaxed text-body/75">{preview.text}</pre>
      </Modal>

      <Modal open={!!deleting} onClose={() => setDeleting(null)} title="确认删除" description={deleting?.item.name}
        footer={<><Button variant="ghost" onClick={() => setDeleting(null)}>取消</Button><Button variant="danger" onClick={confirmDelete}>删除</Button></>}>
        <p className="text-[12px] leading-relaxed text-body/80">
          {deleting?.kind === 'skill' ? '删除后，已装备它的角色会同步卸下。' : '删除后，使用该预设的角色会回到无预设状态。'}
        </p>
      </Modal>

      <p className="mt-4 text-center text-[10.5px] text-muted/50">
        想让角色更像某人，先定一个核心锚点，再挑一个预设与两项技能 ·
        <Link to="/personas" className="ml-1 text-muted hover:text-accent-200">回到角色</Link>
      </p>
    </div>
  )
}
