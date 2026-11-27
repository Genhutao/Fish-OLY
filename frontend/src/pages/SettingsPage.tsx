import { useEffect, useMemo, useRef, useState } from 'react'
import {
  Check, Database, Eye, EyeOff, Gauge, KeyRound, Monitor, Moon, Plug, RotateCcw,
  SlidersHorizontal, Sparkles, Timer, Trash2,
} from 'lucide-react'
import { api, type AppSettings, type AppStats } from '../bridge'
import { applyTheme } from '../lib/theme'
import {
  Button, Field, IconButton, Modal, PageHeader, Segmented, Slider, Switch, cn, useToast,
} from '../components/ui'

type Section = 'model' | 'generation' | 'roam' | 'memory' | 'interface' | 'data'

const SECTIONS: { id: Section; label: string; icon: typeof Plug; hint: string }[] = [
  { id: 'model', label: '模型接入', icon: Plug, hint: '接口、密钥与模型' },
  { id: 'generation', label: '生成参数', icon: SlidersHorizontal, hint: '温度、采样与长度' },
  { id: 'roam', label: '漫游', icon: Timer, hint: '节奏、能耗与发帖' },
  { id: 'memory', label: '记忆', icon: Database, hint: '召回数量与权重' },
  { id: 'interface', label: '界面', icon: Monitor, hint: '密度、动效与输入' },
  { id: 'data', label: '数据', icon: Trash2, hint: '重置与统计' },
]

const PROVIDERS = [
  { id: 'openai', label: 'OpenAI', endpoint: 'https://api.openai.com/v1', model: 'gpt-4o-mini' },
  { id: 'deepseek', label: 'DeepSeek', endpoint: 'https://api.deepseek.com/v1', model: 'deepseek-chat' },
  { id: 'openrouter', label: 'OpenRouter', endpoint: 'https://openrouter.ai/api/v1', model: 'anthropic/claude-3.5-sonnet' },
  { id: 'ollama', label: 'Ollama', endpoint: 'http://127.0.0.1:11434/v1', model: 'qwen2.5:7b' },
]

const DEFAULTS: AppSettings = {
  apiEndpoint: 'https://api.openai.com/v1', apiKey: '', model: 'gpt-4o-mini',
  maxTokens: 2048, temperature: 0.85, topP: 1, presencePenalty: 0.1, frequencyPenalty: 0,
  replyLength: 'normal', memoryTopK: 3, memoryWeightSim: 0.5, memoryWeightImp: 0.3, memoryWeightRec: 0.2,
  roamAutoPost: 0.14, roamRestThreshold: 20, roamEnergyDecay: 1,
  uiDensity: 'comfortable', reduceMotion: false, typewriter: true, enterToSend: true,
  showTimestamps: true, sceneBackground: true, theme: 'dark', githubToken: '',
}

function SectionCard({ title, desc, children }: { title: string; desc: string; children: React.ReactNode }) {
  return (
    <section className="card p-5">
      <header className="mb-4">
        <h2 className="display text-[16px]">{title}</h2>
        <p className="hint mt-1">{desc}</p>
      </header>
      <div className="space-y-5">{children}</div>
    </section>
  )
}

export function SettingsPage() {
  const toast = useToast()
  const [section, setSection] = useState<Section>('model')
  const [s, setS] = useState<AppSettings>(DEFAULTS)
  const [initial, setInitial] = useState<string>('')
  const [savedKey, setSavedKey] = useState('')
  const keyRef = useRef<HTMLInputElement>(null)
  const [interval, setIntervalSec] = useState(8)
  const [stats, setStats] = useState<AppStats | null>(null)
  const [showKey, setShowKey] = useState(false)
  const [testing, setTesting] = useState(false)
  const [testResult, setTestResult] = useState<{ ok: boolean; text: string } | null>(null)
  const [saving, setSaving] = useState(false)
  const [resetOpen, setResetOpen] = useState(false)
  const [resetting, setResetting] = useState(false)

  useEffect(() => {
    api.getSettings().then(v => { setS(v); setInitial(JSON.stringify(v)); setSavedKey(v.apiKey || '') }).catch(console.error)
    api.getRoamState().then(v => setIntervalSec(v.interval)).catch(console.error)
    api.getStats().then(setStats).catch(console.error)
  }, [])

  const dirty = useMemo(() => JSON.stringify(s) !== initial, [s, initial])

  // AUTO-SAVE: persist edits shortly after they settle, so a typed endpoint/key
  // can never be tested against a stale saved config.
  useEffect(() => {
    if (!initial) return
    if (JSON.stringify(s) === initial) return
    const t = setTimeout(() => { persist(true).catch(() => {}) }, 700)
    return () => clearTimeout(t)
  }, [s, initial])

  const patch = (p: Partial<AppSettings>) => setS(prev => ({ ...prev, ...p }))

  const applyUi = (v: AppSettings) => {
    document.documentElement.dataset.density = v.uiDensity
    document.documentElement.dataset.reduceMotion = String(v.reduceMotion)
    applyTheme(v.theme)
  }

  // Read the DOM value too: password managers / IME can fill the field without
  // firing React onChange, which previously left the backend with an empty key.
  const effective = () => ({ ...s, apiKey: (keyRef.current?.value ?? s.apiKey) || '' })

  const persist = async (silent = false) => {
    const next = effective()
    await api.saveSettings(next)
    if (interval !== 8) await api.setRoamInterval(interval)
    applyUi(next)
    setS(next)
    setInitial(JSON.stringify(next))
    setSavedKey(next.apiKey)
    window.dispatchEvent(new Event('oly-settings'))
    if (!silent) toast.push('设置已保存，聊天会立即使用新配置')
  }

  const save = async () => {
    setSaving(true)
    try { await persist() }
    catch (e: any) { toast.push(String(e?.message || e), 'error') }
    finally { setSaving(false) }
  }

  const test = async () => {
    setTesting(true); setTestResult(null)
    try {
      // Test exactly what is in the form, not the last saved snapshot.
      try { await persist(true) } catch { /* the connection test will report it */ }
      const text = await api.testConnection()
      setTestResult({ ok: true, text })
    } catch (e: any) {
      setTestResult({ ok: false, text: String(e?.message || e) })
    } finally { setTesting(false) }
  }

  const reset = async () => {
    setResetting(true)
    try {
      await api.resetDemoData()
      const [st, roam] = await Promise.all([api.getStats(), api.getRoamState()])
      setStats(st); setIntervalSec(roam.interval)
      setResetOpen(false)
      toast.push('演示数据已重置')
    } catch (e: any) {
      toast.push(String(e?.message || e), 'error')
    } finally { setResetting(false) }
  }

  const provider = PROVIDERS.find(p => p.endpoint === s.apiEndpoint)
  const weightSum = s.memoryWeightSim + s.memoryWeightImp + s.memoryWeightRec

  return (
    <div className="mx-auto max-w-[1020px] px-8 py-7">
      <PageHeader
        title="设置"
        subtitle="每一个分支都会真实影响提示词、漫游节奏与界面行为；改动会在 0.7 秒后自动保存并生效。"
        actions={
          <Button onClick={save} loading={saving} disabled={!dirty} icon={dirty ? undefined : <Check size={14} />}>
            {dirty ? '保存设置' : '已保存'}
          </Button>
        }
      />

      <div className="flex gap-6">
        <nav className="w-[168px] shrink-0" aria-label="设置分组">
          <div className="space-y-0.5">
            {SECTIONS.map(({ id, label, icon: Icon, hint }) => (
              <button
                key={id}
                onClick={() => setSection(id)}
                aria-current={section === id}
                className={cn(
                  'group flex w-full items-start gap-2.5 rounded-md px-2.5 py-2 text-left transition-colors',
                  section === id ? 'bg-accent-300/[0.08] text-accent-200' : 'text-muted hover:bg-veil/[0.04] hover:text-body',
                )}
              >
                <Icon size={14} className="mt-0.5 shrink-0" />
                <span>
                  <span className="block text-[12.5px]">{label}</span>
                  <span className="block text-[10px] text-muted/75">{hint}</span>
                </span>
              </button>
            ))}
          </div>
        </nav>

        <div className="min-w-0 flex-1 space-y-4">
          {section === 'model' && (
            <SectionCard title="模型接入" desc="兼容 OpenAI 协议的接口。未填密钥时全部走内置 mock，不会发起网络请求。">
              <div>
                <span className="label">快速选择</span>
                <div className="grid grid-cols-2 gap-2 sm:grid-cols-4">
                  {PROVIDERS.map(p => (
                    <button
                      key={p.id}
                      onClick={() => patch({ apiEndpoint: p.endpoint, model: p.model })}
                      className={cn(
                        'rounded-md border px-3 py-2 text-left transition-colors',
                        provider?.id === p.id ? 'border-accent-300/45 bg-accent-300/[0.08]' : 'border-border hover:border-accent-300/25',
                      )}
                    >
                      <span className="block text-[12px] text-bright">{p.label}</span>
                      <span className="block truncate text-[10px] text-muted">{p.model}</span>
                    </button>
                  ))}
                </div>
              </div>

              <Field label="接口地址" hint="OpenRouter、DeepSeek、本地 Ollama 等均可。">
                <input className="input" value={s.apiEndpoint} onChange={e => patch({ apiEndpoint: e.target.value })} />
              </Field>

              <Field label="API Key" hint="仅保存在本机用户配置目录，不会上传。">
                <div className="flex gap-2">
                  <div className="relative flex-1">
                    <KeyRound size={13} className="pointer-events-none absolute left-3 top-1/2 -translate-y-1/2 text-muted" />
                    <input
                      ref={keyRef}
                      autoComplete="off"
                      name="oly-persona-api-key"
                      className="input pl-8 pr-9"
                      type={showKey ? 'text' : 'password'}
                      placeholder="sk-…"
                      value={s.apiKey}
                      onChange={e => patch({ apiKey: e.target.value })}
                    />
                    <button
                      type="button"
                      aria-label={showKey ? '隐藏密钥' : '显示密钥'}
                      onClick={() => setShowKey(v => !v)}
                      className="absolute right-1.5 top-1/2 -translate-y-1/2 rounded p-1.5 text-muted hover:text-body"
                    >
                      {showKey ? <EyeOff size={13} /> : <Eye size={13} />}
                    </button>
                  </div>
                  <Button variant="outline" onClick={test} loading={testing} icon={<Plug size={13} />}>测试连接</Button>
                  <Button variant="ghost" onClick={async () => { await save(); await test() }} className="px-2" title="先保存再测试">
                    保存并测试
                  </Button>
                </div>
                {testResult && (
                  <p className={cn('mt-2 flex items-start gap-1.5 text-[11px]', testResult.ok ? 'text-aurora' : 'text-ember')}>
                    <span className="mt-1 h-1 w-1 shrink-0 rounded-full bg-current" />{testResult.text}
                  </p>
                )}
              </Field>

              <div className="grid grid-cols-2 gap-4">
                <Field label="模型">
                  <input className="input" list="model-suggestions" value={s.model} onChange={e => patch({ model: e.target.value })} />
                  <datalist id="model-suggestions">
                    {['gpt-4o-mini', 'gpt-4o', 'deepseek-chat', 'deepseek-reasoner', 'qwen2.5:7b', 'claude-3.5-sonnet'].map(m => <option key={m} value={m} />)}
                  </datalist>
                </Field>
                <Field label="最大输出 Token">
                  <input className="input font-mono" type="number" min={256} max={32000} step={256}
                    value={s.maxTokens} onChange={e => patch({ maxTokens: Number(e.target.value) })} />
                </Field>
              </div>

              <Field label="GitHub Token（可选）" hint="技能库从 GitHub 搜索/导入时使用，可提高 API 限流额度。">
                <input className="input font-mono text-[11px]" placeholder="ghp_…"
                  value={s.githubToken} onChange={e => patch({ githubToken: e.target.value })} />
              </Field>

              {!s.apiKey ? (
                <p className="rounded-md border border-warning/25 bg-warning/[0.06] px-3 py-2 text-[11px] leading-relaxed text-warning/90">
                  还没有密钥，聊天会使用内置 mock 回复。填好 Key 后点「保存设置」即可接入真实模型。
                </p>
              ) : s.apiKey !== savedKey ? (
                <p className="rounded-md border border-accent-300/30 bg-accent-300/[0.07] px-3 py-2 text-[11px] leading-relaxed text-accent-200">
                  已输入新的 Key，但还没保存；当前聊天仍使用之前保存的配置。
                </p>
              ) : (
                <p className="rounded-md border border-aurora/25 bg-aurora/[0.06] px-3 py-2 text-[11px] leading-relaxed text-aurora">
                  已配置：{s.model} · {s.apiEndpoint}。对话会真实请求这个接口。
                </p>
              )}
            </SectionCard>
          )}

          {section === 'generation' && (
            <SectionCard title="生成参数" desc="这些值直接进入 /chat/completions 请求，并影响系统提示词里的长度约束。">
              <div className="grid grid-cols-1 gap-5 sm:grid-cols-2">
                <Slider label="Temperature" value={s.temperature} min={0} max={2} step={0.05}
                  format={v => v.toFixed(2)} onChange={v => patch({ temperature: v })} />
                <Slider label="Top P" value={s.topP} min={0.1} max={1} step={0.05}
                  format={v => v.toFixed(2)} onChange={v => patch({ topP: v })} />
                <Slider label="存在惩罚" value={s.presencePenalty} min={-2} max={2} step={0.1}
                  format={v => v.toFixed(1)} onChange={v => patch({ presencePenalty: v })} />
                <Slider label="频率惩罚" value={s.frequencyPenalty} min={-2} max={2} step={0.1}
                  format={v => v.toFixed(1)} onChange={v => patch({ frequencyPenalty: v })} />
              </div>
              <Field label="回复长度" hint="同时约束提示词与前端展示节奏。">
                <Segmented
                  value={s.replyLength}
                  onChange={v => patch({ replyLength: v })}
                  options={[
                    { value: 'short', label: '短 · 80 字' },
                    { value: 'normal', label: '中 · 150 字' },
                    { value: 'long', label: '长 · 320 字' },
                  ]}
                />
              </Field>
              <p className="hint">温度越高，角色越容易偏离核心锚点；demo 里建议 0.7–1.0。</p>
            </SectionCard>
          )}

          {section === 'roam' && (
            <SectionCard title="漫游" desc="角色的精力消耗、休息阈值与发帖概率。改动在下一拍生效。">
              <Field label="推进间隔" hint={`每 ${interval} 秒一拍；运行中修改会重启计时器。`}>
                <Segmented
                  value={String(interval)}
                  onChange={v => setIntervalSec(Number(v))}
                  options={[{ value: '4', label: '4 秒' }, { value: '8', label: '8 秒' }, { value: '16', label: '16 秒' }]}
                />
              </Field>
              <Slider label="精力消耗倍率" value={s.roamEnergyDecay} min={0.2} max={3} step={0.1}
                format={v => `${v.toFixed(1)}×`} onChange={v => patch({ roamEnergyDecay: v })} />
              <Slider label="休息阈值" value={s.roamRestThreshold} min={5} max={80} step={1}
                format={v => `${v} 精力`} onChange={v => patch({ roamRestThreshold: v })} />
              <Slider label="发帖概率 / 拍" value={s.roamAutoPost} min={0} max={1} step={0.01}
                format={v => `${Math.round(v * 100)}%`} onChange={v => patch({ roamAutoPost: v })} />
            </SectionCard>
          )}

          {section === 'memory' && (
            <SectionCard title="记忆" desc="召回打分 = 相似度 × w1 + 重要度 × w2 + 时效 × w3，权重会自动归一化。">
              <Field label="召回条数" hint="每次对话注入的最近记忆条数。">
                <div className="flex items-center gap-2">
                  {[2, 3, 5, 8].map(n => (
                    <button key={n} onClick={() => patch({ memoryTopK: n })}
                      className={cn('h-8 w-10 rounded-md border font-mono text-[12px] transition-colors',
                        s.memoryTopK === n ? 'border-accent-300/45 bg-accent-300/10 text-accent-200' : 'border-border text-muted hover:text-body')}>
                      {n}
                    </button>
                  ))}
                </div>
              </Field>
              <Slider label="相似度权重" value={s.memoryWeightSim} min={0} max={1} step={0.05}
                format={v => `${Math.round(v * 100)}%`} onChange={v => patch({ memoryWeightSim: v })} />
              <Slider label="重要度权重" value={s.memoryWeightImp} min={0} max={1} step={0.05}
                format={v => `${Math.round(v * 100)}%`} onChange={v => patch({ memoryWeightImp: v })} />
              <Slider label="时效权重" value={s.memoryWeightRec} min={0} max={1} step={0.05}
                format={v => `${Math.round(v * 100)}%`} onChange={v => patch({ memoryWeightRec: v })} />
              <p className="hint">当前权重和 {Math.round(weightSum * 100)}%，保存时按比例归一。</p>
            </SectionCard>
          )}

          {section === 'interface' && (
            <SectionCard title="界面" desc="密度与动效会立即应用到当前窗口；其余开关在保存后生效。">
              <Field label="界面模式" hint="也可以点顶栏的太阳/月亮切换；切换时会以点击位置为圆心展开。">
                <Segmented
                  value={s.theme}
                  onChange={v => { patch({ theme: v }); applyTheme(v) }}
                  options={[
                    { value: 'dark', label: '黑夜' },
                    { value: 'light', label: '白昼' },
                    { value: 'system', label: '跟随系统' },
                  ]}
                />
              </Field>
              <Field label="信息密度">
                <Segmented
                  value={s.uiDensity}
                  onChange={v => { patch({ uiDensity: v }); document.documentElement.dataset.density = v }}
                  options={[{ value: 'comfortable', label: '舒展' }, { value: 'compact', label: '紧凑' }]}
                />
              </Field>
              {([
                ['reduceMotion', '减少动效', '关闭页面切换与装饰动画，保留状态反馈。'],
                ['typewriter', '打字机输出', 'AI 回复逐字出现；关闭后直接显示整段。'],
                ['enterToSend', '回车发送', '关闭后使用 ⌘/Ctrl + Enter 发送。'],
                ['showTimestamps', '显示时间戳', '在消息悬停时显示。'],
                ['sceneBackground', '场景氛围底纹', '漫游与社区页显示环境色晕。'],
              ] as const).map(([key, label, hint]) => (
                <div key={key} className="flex items-start justify-between gap-6 border-b border-border/60 pb-4 last:border-0 last:pb-0">
                  <div>
                    <p className="text-[12.5px] text-body">{label}</p>
                    <p className="hint mt-0.5">{hint}</p>
                  </div>
                  <Switch checked={s[key] as boolean} onChange={v => patch({ [key]: v } as Partial<AppSettings>)} label={label} />
                </div>
              ))}
            </SectionCard>
          )}

          {section === 'data' && (
            <>
              <SectionCard title="演示数据" desc="数据保存在运行内存中，重启会回到初始种子；设置文件例外。">
                <div className="grid grid-cols-2 gap-3 sm:grid-cols-4">
                  {[
                    ['角色', stats?.personas ?? 0], ['记忆', stats?.memories ?? 0],
                    ['社区动态', stats?.posts ?? 0], ['漫游拍数', stats?.ticks ?? 0],
                  ].map(([label, v]) => (
                    <div key={label as string} className="rounded-md border border-border bg-veil/[0.03] px-3 py-2.5">
                      <p className="text-[10px] text-muted">{label}</p>
                      <p className="mt-0.5 font-mono text-[18px] tabular-nums text-bright">{v}</p>
                    </div>
                  ))}
                </div>
                <div className="flex items-center justify-between rounded-md border border-ember/25 bg-ember/[0.05] px-3 py-2.5">
                  <div>
                    <p className="text-[12px] text-body">重置演示数据</p>
                    <p className="hint mt-0.5">清空人设、剧情、社区与漫游状态，重新载入种子数据。</p>
                  </div>
                  <Button variant="danger" size="sm" onClick={() => setResetOpen(true)} icon={<RotateCcw size={13} />}>重置</Button>
                </div>
              </SectionCard>
              <SectionCard title="关于" desc="OLY Persona 是一个可演示的人设扮演工作台。">
                <div className="grid grid-cols-2 gap-3 text-[11.5px] text-muted">
                  <p>版本 0.2.0 · Go + Wails + React</p>
                  <p>技能 {stats?.skills ?? 0} · 预设 {stats?.presets ?? 0}</p>
                  <p>向量：32 维 mock embedding</p>
                  <p>存储：内存 + 设置 JSON</p>
                </div>
                <p className="hint">后续可替换为 SQLite + 真实 embedding，schema 已按表设计。</p>
              </SectionCard>
            </>
          )}
        </div>
      </div>

      <Modal
        open={resetOpen}
        onClose={() => setResetOpen(false)}
        title="重置演示数据？"
        description="这会清空当前所有角色、剧情、社区动态和漫游记录，并重新载入种子数据。"
        footer={
          <>
            <Button variant="ghost" onClick={() => setResetOpen(false)}>取消</Button>
            <Button variant="danger" loading={resetting} onClick={reset} icon={<Trash2 size={13} />}>确认重置</Button>
          </>
        }
      >
        <p className="text-[12px] leading-relaxed text-body/80">
          设置与密钥会保留。演示里常用的做法：重置后先把两位角色放进漫游，再看看社区会不会出现新的日记。
        </p>
      </Modal>
    </div>
  )
}
