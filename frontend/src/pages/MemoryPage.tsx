import { useEffect, useState } from 'react'
import { useNavigate, useParams } from 'react-router-dom'
import { ArrowLeft, Clock, Plus, RotateCcw, Search, Waypoints } from 'lucide-react'
import { api, type MemoryEntry, type MemorySnapshot, type Persona } from '../bridge'
import { Button, EmptyState, PageHeader, Segmented, cn, useToast } from '../components/ui'

const REASON_LABEL: Record<string, string> = {
  initial: '初始版本',
  user_edit: '手动编辑',
  preset_applied: '套用预设',
  skill_toggle: '调整技能',
  rollback: '回退恢复',
}

export function MemoryPage() {
  const { id } = useParams<{ id: string }>()
  const nav = useNavigate()
  const toast = useToast()
  const [persona, setPersona] = useState<Persona | null>(null)
  const [snapshots, setSnapshots] = useState<MemorySnapshot[]>([])
  const [memories, setMemories] = useState<MemoryEntry[]>([])
  const [recallQuery, setRecallQuery] = useState('')
  const [recalled, setRecalled] = useState<MemoryEntry[]>([])
  const [recalling, setRecalling] = useState(false)
  const [newMemory, setNewMemory] = useState('')
  const [tab, setTab] = useState<'timeline' | 'vectors'>('timeline')

  useEffect(() => {
    if (!id) return
    api.getPersona(id).then(setPersona).catch(console.error)
    api.getSnapshots(id).then(list => setSnapshots(list || [])).catch(console.error)
    api.getMemories(id).then(list => setMemories(list || [])).catch(console.error)
  }, [id])

  const rollback = async (snapId: string) => {
    if (!id) return
    try {
      const p = await api.rollbackToSnapshot(id, snapId)
      setPersona(p)
      const [snaps, mems] = await Promise.all([api.getSnapshots(id), api.getMemories(id)])
      setSnapshots(snaps); setMemories(mems)
      toast.push('已回退到该快照')
    } catch (e: any) { toast.push(String(e?.message || e), 'error') }
  }

  const addMem = async () => {
    if (!id || !newMemory.trim()) return
    await api.addMemory(id, newMemory.trim(), 0.7, ['手动'])
    setNewMemory('')
    api.getMemories(id).then(list => setMemories(list || []))
    toast.push('记忆已写入')
  }

  const recall = async () => {
    if (!id || !recallQuery.trim()) return
    setRecalling(true)
    try { setRecalled(await api.recallMemories(id, recallQuery, 5)) }
    catch (e: any) { toast.push(String(e?.message || e), 'error') }
    finally { setRecalling(false) }
  }

  if (!persona) return <div className="px-8 py-10 text-[12px] text-muted">正在读取记忆…</div>

  return (
    <div className="mx-auto max-w-[880px] px-8 py-7">
      <button onClick={() => nav(`/personas/${id}`)} className="btn-ghost mb-5 -ml-3 h-8 px-2 text-[11.5px]">
        <ArrowLeft size={14} /> 返回对话
      </button>

      <PageHeader
        title={`${persona.name} 的记忆`}
        subtitle="快照记录人格版本，记忆库记录它经历过的事。两者都可以回看与回退。"
        aside={<span className="tag tag-gold">v{persona.evoVersion}</span>}
      />

      <div className="mb-5">
        <Segmented
          value={tab}
          onChange={setTab}
          options={[
            { value: 'timeline', label: `时间线 ${snapshots.length}` },
            { value: 'vectors', label: `记忆 ${memories.length}` },
          ]}
        />
      </div>

      {tab === 'timeline' && (
        snapshots.length === 0 ? (
          <EmptyState icon={<Waypoints size={18} />} title="还没有快照" description="修改人设、套用预设或调整技能时会自动创建快照。" />
        ) : (
          <ol className="relative ml-2 space-y-3 border-l border-border pl-6">
            {snapshots.map((s, i) => (
              <li key={s.id} className="relative">
                <span className={cn(
                  'absolute -left-[31px] top-4 grid h-3 w-3 place-items-center rounded-full border',
                  i === 0 ? 'border-accent-300 bg-accent-300/30' : 'border-border bg-field',
                )}>
                  <span className={cn('h-1 w-1 rounded-full', i === 0 ? 'bg-accent-200' : 'bg-muted/50')} />
                </span>
                <div className={cn('card p-4', i === 0 && 'border-accent-300/25')}>
                  <div className="flex items-start justify-between gap-4">
                    <div className="min-w-0">
                      <div className="flex items-center gap-2">
                        <span className="font-mono text-[12px] text-body">v{s.version}</span>
                        <span className="text-[12px] text-bright">{REASON_LABEL[s.reason] || s.reason}</span>
                        {i === 0 && <span className="tag tag-gold text-[9.5px]">当前</span>}
                        <span className="ml-auto flex items-center gap-1 text-[10px] text-muted/60">
                          <Clock size={10} />{new Date(s.createdAt).toLocaleString('zh-CN', { month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit' })}
                        </span>
                      </div>
                      <p className="quote mt-1.5 line-clamp-1 text-[11.5px]">「{s.state.coreAnchor}」</p>
                    </div>
                    {i > 0 && (
                      <Button variant="outline" size="sm" className="h-7 shrink-0" onClick={() => rollback(s.id)} icon={<RotateCcw size={11} />}>
                        回退
                      </Button>
                    )}
                  </div>
                </div>
              </li>
            ))}
          </ol>
        )
      )}

      {tab === 'vectors' && (
        <div className="space-y-4">
          <section className="card p-4">
            <p className="section-title">按当前情境召回</p>
            <div className="flex gap-2">
              <input className="input flex-1" placeholder="输入一句话，按相似度 + 重要度 + 时效混合检索"
                value={recallQuery} onChange={e => setRecallQuery(e.target.value)}
                onKeyDown={e => { if (e.key === 'Enter') recall() }} aria-label="召回检索" />
              <Button onClick={recall} loading={recalling} disabled={!recallQuery.trim()} icon={<Search size={13} />}>回忆</Button>
            </div>
            {recalled.length > 0 && (
              <div className="mt-3 space-y-2">
                {recalled.map(m => (
                  <div key={m.id} className="rounded-md border border-accent-300/20 bg-accent-300/[0.05] px-3 py-2">
                    <div className="mb-1 flex items-center gap-2 text-[10px] text-accent-200/80">
                      <span>重要度 {(m.important * 10).toFixed(0)}/10</span>
                      {(m.tags || []).map((t, i) => <span key={i} className="tag text-[9.5px]">{t}</span>)}
                    </div>
                    <p className="text-[12px] leading-relaxed text-body/85">{m.content}</p>
                  </div>
                ))}
              </div>
            )}
          </section>

          <section className="card p-4">
            <p className="section-title">手动写入一条记忆</p>
            <div className="flex gap-2">
              <input className="input flex-1" placeholder="她会记住的事…"
                value={newMemory} onChange={e => setNewMemory(e.target.value)}
                onKeyDown={e => { if (e.key === 'Enter') addMem() }} aria-label="新记忆" />
              <Button onClick={addMem} disabled={!newMemory.trim()} icon={<Plus size={13} />}>写入</Button>
            </div>
          </section>

          <section>
            <p className="section-title">全部记忆 · {memories.length}</p>
            {memories.length === 0 ? (
              <EmptyState icon={<Waypoints size={18} />} title="记忆库是空的" description="对话超过 20 字会自动写入一条，也可以手动添加。" />
            ) : (
              <div className="space-y-2">
                {memories.map(m => (
                  <div key={m.id} className="card px-4 py-3">
                    <div className="mb-1 flex items-center gap-2">
                      <Clock size={11} className="text-muted/60" />
                      <span className="text-[10px] text-muted/60">{new Date(m.createdAt).toLocaleString('zh-CN')}</span>
                      <span className="ml-auto text-[10px] text-accent-200/70">{(m.important * 10).toFixed(0)}/10</span>
                      {(m.tags || []).map((t, i) => <span key={i} className="tag text-[9.5px]">{t}</span>)}
                    </div>
                    <p className="text-[12px] leading-relaxed text-body/80">{m.content}</p>
                  </div>
                ))}
              </div>
            )}
          </section>
        </div>
      )}
    </div>
  )
}
