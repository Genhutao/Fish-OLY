import { useState } from 'react'
import { useNavigate } from 'react-router-dom'
import { ArrowLeft, BookOpen, FileText, Sparkles } from 'lucide-react'
import { api, type Persona } from '../bridge'
import { Button, Field, PageHeader, Segmented, cn, useToast } from '../components/ui'

const MOODS = [
  ['neutral', '平常'], ['calm', '平静'], ['happy', '愉快'], ['sad', '低落'], ['angry', '恼火'],
] as const

const DIMENSIONS = ['身份', '兴趣', '观点', '经历', '成就', '社交关系', '其他']

export function PersonaCreatePage() {
  const nav = useNavigate()
  const toast = useToast()
  const [mode, setMode] = useState<'manual' | 'extract'>('manual')
  const [rawText, setRawText] = useState('')
  const [extracting, setExtracting] = useState(false)
  const [form, setForm] = useState<Partial<Persona>>({
    name: '', avatar: '🧩', description: '', personality: '',
    coreAnchor: '', mood: 'neutral', tags: [],
  })
  const [tagInput, setTagInput] = useState('')
  const [saving, setSaving] = useState(false)

  const addTag = () => {
    const t = tagInput.trim()
    if (t && !(form.tags || []).includes(t)) setForm({ ...form, tags: [...(form.tags || []), t] })
    setTagInput('')
  }

  const extract = async () => {
    if (!rawText.trim()) return
    setExtracting(true)
    try {
      const p = await api.aiExtractPersona(rawText)
      setForm({
        name: p.name, avatar: form.avatar || '🧩', description: p.description,
        personality: p.personality, coreAnchor: p.coreAnchor,
        mood: p.mood || 'neutral', tags: p.tags || [],
      })
      setMode('manual')
      toast.push('已抽取，可继续手动修改')
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
    finally { setExtracting(false) }
  }

  const save = async () => {
    if (!form.name?.trim()) return
    setSaving(true)
    try {
      const created = await api.createPersona(form)
      toast.push('人设已创建')
      nav(`/personas/${created.id}`)
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
    finally { setSaving(false) }
  }

  return (
    <div className="mx-auto max-w-[1000px] px-8 py-7">
      <button onClick={() => nav(-1)} className="btn-ghost mb-5 -ml-3 h-8 px-2 text-[11.5px]">
        <ArrowLeft size={14} /> 返回
      </button>
      <PageHeader
        title="创建人设"
        subtitle="七维属性决定它知道什么，核心锚点决定它不会变成谁。"
        actions={<Button onClick={save} loading={saving} disabled={!form.name?.trim()}>保存人设</Button>}
      />

      <div className="mb-5">
        <Segmented
          value={mode}
          onChange={setMode}
          options={[
            { value: 'manual', label: <span className="flex items-center gap-1.5"><FileText size={12} /> 手动</span> },
            { value: 'extract', label: <span className="flex items-center gap-1.5"><BookOpen size={12} /> 文本抽取</span> },
          ]}
        />
      </div>

      <div className="grid grid-cols-1 gap-5 lg:grid-cols-[1fr_280px]">
        <div className="space-y-4">
          {mode === 'extract' && (
            <section className="card p-5">
              <Field label="粘贴角色设定 / 小说片段 / 任意文本" hint="AI 会抽取名称、描述、七维人格与锚点；没有密钥时使用 mock 规则。">
                <textarea
                  className="input min-h-[150px] font-sans"
                  placeholder="把角色描写、小说章节或包含角色信息的文本粘贴到这里…"
                  value={rawText}
                  onChange={e => setRawText(e.target.value)}
                />
              </Field>
              <div className="mt-3 flex items-center gap-3">
                <Button onClick={extract} loading={extracting} disabled={!rawText.trim()} icon={<Sparkles size={13} />}>
                  抽取人设
                </Button>
                <span className="hint">抽取后不会直接保存，你可以继续修改。</span>
              </div>
            </section>
          )}

          <section className="card p-5">
            <div className="grid grid-cols-[86px_1fr] gap-4">
              <Field label="头像">
                <input className="input text-center text-2xl" value={form.avatar} maxLength={4}
                  onChange={e => setForm({ ...form, avatar: e.target.value })} aria-label="头像 emoji" />
              </Field>
              <Field label="名称" hint="角色被人记住的第一个东西。">
                <input className="input" placeholder="苍月千鹤" value={form.name}
                  onChange={e => setForm({ ...form, name: e.target.value })} />
              </Field>
            </div>

            <div className="mt-4">
              <Field label="核心描述" hint="身份、外貌、背景，2–3 句话即可。">
                <textarea className="input min-h-[78px]" placeholder="银发红瞳的古风少女，出身没落贵族…"
                  value={form.description} onChange={e => setForm({ ...form, description: e.target.value })} />
              </Field>
            </div>

            <div className="mt-4">
              <Field label="人格七维" hint="用 | 分隔：身份 | 兴趣 | 观点 | 经历 | 成就 | 社交关系 | 其他">
                <textarea className="input min-h-[78px]" placeholder="身份：没落贵族之后 | 兴趣：古琴、剑术 …"
                  value={form.personality} onChange={e => setForm({ ...form, personality: e.target.value })} />
              </Field>
            </div>

            <div className="mt-4">
              <Field label="核心锚点" hint="一句不可变的话；人格演化与漫游都不会改写它。">
                <input className="input" placeholder="用疏离掩藏温柔，从不先承认在意"
                  value={form.coreAnchor} onChange={e => setForm({ ...form, coreAnchor: e.target.value })} />
              </Field>
            </div>

            <div className="mt-4 grid grid-cols-2 gap-4">
              <Field label="初始心情">
                <select className="input" value={form.mood} onChange={e => setForm({ ...form, mood: e.target.value })}>
                  {MOODS.map(([v, label]) => <option key={v} value={v}>{label}</option>)}
                </select>
              </Field>
              <Field label="标签" hint="回车添加，点击标签移除。">
                <input className="input" placeholder="古风 / 冷淡系" value={tagInput}
                  onChange={e => setTagInput(e.target.value)}
                  onKeyDown={e => { if (e.key === 'Enter') { e.preventDefault(); addTag() } }} />
              </Field>
            </div>

            {(form.tags || []).length > 0 && (
              <div className="mt-3 flex flex-wrap gap-1.5">
                {(form.tags || []).map((t, i) => (
                  <button key={i} onClick={() => setForm({ ...form, tags: (form.tags || []).filter((_, j) => j !== i) })}
                    className="tag hover:border-ember/40 hover:text-ember">
                    {t} <span className="text-[9px]">×</span>
                  </button>
                ))}
              </div>
            )}
          </section>
        </div>

        <aside className="space-y-4">
          <div className="card p-4">
            <p className="section-title">七维自检</p>
            <ul className="space-y-1.5">
              {DIMENSIONS.map((d, i) => {
                const filled = (form.personality || '').split('|').length > i
                return (
                  <li key={d} className="flex items-center gap-2 text-[11.5px]">
                    <span className={cn('h-1.5 w-1.5 rounded-full', filled ? 'bg-accent-300' : 'bg-border')} />
                    <span className={filled ? 'text-body/80' : 'text-muted/50'}>{d}</span>
                  </li>
                )
              })}
            </ul>
            <p className="hint mt-3">缺哪一维就补哪一维，比堆形容词有效。</p>
          </div>
          <div className="card p-4">
            <p className="section-title">写好锚点的三个问题</p>
            <ol className="space-y-2 text-[11px] leading-relaxed text-muted">
              <li>1. 它绝不会主动说什么？</li>
              <li>2. 它用什么方式掩饰在意？</li>
              <li>3. 哪句话在 100 轮后仍成立？</li>
            </ol>
          </div>
        </aside>
      </div>
    </div>
  )
}
