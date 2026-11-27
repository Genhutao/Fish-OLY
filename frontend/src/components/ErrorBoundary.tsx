import { Component, type ErrorInfo, type ReactNode } from 'react'

type Props = { children: ReactNode }
type State = { error: Error | null }

export class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null }

  static getDerivedStateFromError(error: Error): State {
    return { error }
  }

  componentDidCatch(error: Error, info: ErrorInfo) {
    console.error('page crashed', error, info.componentStack)
  }

  render() {
    if (this.state.error) {
      return (
        <div className="mx-auto max-w-2xl px-8 py-10">
          <div className="card border-ember/40 p-5">
            <h2 className="display text-[18px] text-ember">这个页面崩了</h2>
            <p className="mt-2 text-[12px] text-body/80">把下面的信息发给开发者即可定位。</p>
            <pre className="mt-3 max-h-[50vh] overflow-auto whitespace-pre-wrap rounded-md bg-field p-3 font-mono text-[10.5px] leading-relaxed text-body/70">
              {this.state.error.stack || this.state.error.message}
            </pre>
            <button className="btn-primary mt-4 h-8 px-3 text-[12px]" onClick={() => this.setState({ error: null })}>
              重试
            </button>
          </div>
        </div>
      )
    }
    return this.props.children
  }
}
