'use client';

import { useEffect, useMemo, useState } from 'react';
import { useRouter } from 'next/navigation';
import { useAuth } from '@/components/AuthContext';
import { AddPatientModal, PatientsList } from '@/components/Patients';

interface User {
  email: string;
  id: string;
}

const getInitials = (email: string) => {
  const local = email.split('@')[0] ?? '';
  return local.slice(0, 2).toUpperCase() || '?';
};

export default function DashboardPage() {
  const [user, setUser] = useState<User | null>(null);
  const [authChecked, setAuthChecked] = useState(false);
  const [showAddModal, setShowAddModal] = useState(false);
  const { token, logout } = useAuth();
  const router = useRouter();

  useEffect(() => {
    if (!token) {
      router.push('/login');
      return;
    }

    let cancelled = false;
    fetch('http://localhost:8000/auth/me', {
      headers: {
        'Authorization': `Bearer ${token}`,
      },
    })
      .then((res) => {
        if (!res.ok) {
          logout();
          router.push('/login');
          throw new Error('Unauthorized');
        }
        return res.json();
      })
      .then((data) => {
        if (!cancelled) setUser(data);
      })
      .catch(() => {
        if (!cancelled) {
          logout();
          router.push('/login');
        }
      })
      .finally(() => {
        if (!cancelled) setAuthChecked(true);
      });

    return () => {
      cancelled = true;
    };
  }, [token, logout, router]);

  const initials = useMemo(() => (user ? getInitials(user.email) : '?'), [user]);

  if (!authChecked || !user) {
    return (
      <div className="app-shell">
        <div className="app-main">
          <div className="stack">
            <div className="skeleton" style={{ height: 28, width: 220 }} />
            <div className="skeleton" style={{ height: 16, width: 320 }} />
            <div className="patients-grid" aria-hidden="true">
              {[0, 1, 2].map((i) => (
                <div key={i} className="patient-card">
                  <div className="skeleton" style={{ height: 44, width: 44, borderRadius: '50%' }} />
                  <div className="skeleton" style={{ height: 16, width: '60%' }} />
                  <div className="skeleton" style={{ height: 12, width: '40%' }} />
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>
    );
  }

  return (
    <div className="app-shell">
      <header className="app-header">
        <div className="app-header__brand">
          <span className="app-header__brand-mark" aria-hidden="true">P</span>
          <span>Patient Portal</span>
        </div>

        <div className="app-header__user">
          <div className="row" style={{ gap: 10 }}>
            <div className="avatar" aria-hidden="true">{initials}</div>
            <div style={{ lineHeight: 1.2 }}>
              <div style={{ fontWeight: 600, fontSize: 14 }}>{user.email}</div>
              <div className="subtle">Signed in</div>
            </div>
          </div>
          <button type="button" className="btn btn--ghost" onClick={logout}>
            Log out
          </button>
        </div>
      </header>

      <main className="app-main">
        <h1 className="page-title">Patients</h1>
        <p className="page-subtitle">
          View and manage patients in your care. Add a new patient to keep their records up to date.
        </p>

        <section>
          <div className="section-header">
            <div>
              <h2 className="section-title">All patients</h2>
              <div className="section-meta">Records stored securely in your account.</div>
            </div>
            <button
              type="button"
              className="btn btn--primary"
              onClick={() => setShowAddModal(true)}
            >
              <span aria-hidden="true">＋</span> Add patient
            </button>
          </div>

          <PatientsList onAddRequest={() => setShowAddModal(true)} />
        </section>
      </main>

      <AddPatientModal
        open={showAddModal}
        onClose={() => setShowAddModal(false)}
        onCreated={() => {
          // PatientsList reloads itself on its own; nothing else to do here.
        }}
      />
    </div>
  );
}
