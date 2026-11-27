import { Routes, Route, Navigate } from 'react-router-dom'
import { AppShell } from './components/Shell'
import { ToastProvider } from './components/ui'
import { PersonasPage } from './pages/PersonasPage'
import { PersonaDetailPage } from './pages/PersonaDetailPage'
import { PersonaCreatePage } from './pages/PersonaCreatePage'
import { MemoryPage } from './pages/MemoryPage'
import { CommunityPage } from './pages/CommunityPage'
import { StoryPage } from './pages/StoryPage'
import { StoryDetailPage } from './pages/StoryDetailPage'
import { SettingsPage } from './pages/SettingsPage'
import { DesktopPetPage } from './pages/DesktopPetPage'
import { RoamPage } from './pages/RoamPage'
import { ChatPage } from './pages/ChatPage'
import { LibraryPage } from './pages/LibraryPage'

export default function App() {
  return (
    <ToastProvider>
      <AppShell>
        <Routes>
          <Route path="/" element={<Navigate to="/chat" replace />} />
          <Route path="/chat" element={<ChatPage />} />
          <Route path="/personas" element={<PersonasPage />} />
          <Route path="/personas/:id" element={<PersonaDetailPage />} />
          <Route path="/personas/new" element={<PersonaCreatePage />} />
          <Route path="/memory/:id" element={<MemoryPage />} />
          <Route path="/community" element={<CommunityPage />} />
          <Route path="/story" element={<StoryPage />} />
          <Route path="/story/:id" element={<StoryDetailPage />} />
          <Route path="/settings" element={<SettingsPage />} />
          <Route path="/pet" element={<DesktopPetPage />} />
          <Route path="/roam" element={<RoamPage />} />
          <Route path="/library" element={<LibraryPage />} />
          <Route path="*" element={<Navigate to="/chat" replace />} />
        </Routes>
      </AppShell>
    </ToastProvider>
  )
}
