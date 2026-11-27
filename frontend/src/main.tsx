import React from 'react'
import ReactDOM from 'react-dom/client'
import { HashRouter } from 'react-router-dom'
import App from './App'
import { initTheme } from './lib/theme'
import './styles/globals.css'

function fatal(message: string) {
  const el = document.createElement('pre')
  el.style.cssText = 'position:fixed;inset:0;z-index:99999;background:#111;color:#f2c;padding:24px;font:12px/1.5 monospace;white-space:pre-wrap;overflow:auto'
  el.textContent = 'OLY crashed:\n\n' + message
  document.body.appendChild(el)
}

window.addEventListener('error', e => fatal(e.error?.stack || e.message))
window.addEventListener('unhandledrejection', e => fatal(String((e.reason && (e.reason.stack || e.reason.message)) || e.reason)))

initTheme()

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <HashRouter>
      <App />
    </HashRouter>
  </React.StrictMode>
)
