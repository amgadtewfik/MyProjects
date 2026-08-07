'use client';

import { FormEvent, useEffect, useMemo, useState } from 'react';
import { useAuth } from '@/components/AuthContext';

interface Patient {
  id: number | string;
  name: string;
  date_of_birth: string;
}

const API_BASE_URL = 'http://localhost:8000';

const getInitials = (name: string) =>
  name
    .trim()
    .split(/\s+/)
    .slice(0, 2)
    .map((part) => part[0]?.toUpperCase() ?? '')
    .join('') || '?';

const formatDate = (value: string) => {
  // Accept either ISO (YYYY-MM-DD) or DD-MM-YYYY and render as a long, locale-friendly string.
  if (!value) return '';
  let iso = value;
  const dashMatch = value.match(/^(\d{2})-(\d{2})-(\d{4})$/);
  if (dashMatch) {
    iso = `${dashMatch[3]}-${dashMatch[2]}-${dashMatch[1]}`;
  }
  const date = new Date(iso);
  if (Number.isNaN(date.getTime())) return value;
  return date.toLocaleDateString(undefined, {
    year: 'numeric',
    month: 'long',
    day: 'numeric',
  });
};

const computeAge = (value: string): number | null => {
  if (!value) return null;
  let iso = value;
  const dashMatch = value.match(/^(\d{2})-(\d{2})-(\d{4})$/);
  if (dashMatch) {
    iso = `${dashMatch[3]}-${dashMatch[2]}-${dashMatch[1]}`;
  }
  const dob = new Date(iso);
  if (Number.isNaN(dob.getTime())) return null;
  const now = new Date();
  let age = now.getFullYear() - dob.getFullYear();
  const monthDiff = now.getMonth() - dob.getMonth();
  if (monthDiff < 0 || (monthDiff === 0 && now.getDate() < dob.getDate())) {
    age -= 1;
  }
  return age >= 0 ? age : null;
};

const isValidDate = (value: string) => {
  if (!value) return false;
  const date = new Date(value);
  return !Number.isNaN(date.getTime());
};

export interface AddPatientModalProps {
  open: boolean;
  onClose: () => void;
  onCreated: (patient: Patient) => void;
}

export function AddPatientModal({ open, onClose, onCreated }: AddPatientModalProps) {
  const { token } = useAuth();
  const [name, setName] = useState('');
  const [dateOfBirth, setDateOfBirth] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    if (!open) {
      setName('');
      setDateOfBirth('');
      setError(null);
      setSubmitting(false);
    }
  }, [open]);

  useEffect(() => {
    if (!open) return;
    const onKey = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    window.addEventListener('keydown', onKey);
    return () => window.removeEventListener('keydown', onKey);
  }, [open, onClose]);

  if (!open) return null;

  const handleSubmit = async (e: FormEvent) => {
    e.preventDefault();
    setError(null);

    if (!name.trim()) {
      setError('Please enter a patient name.');
      return;
    }
    if (!isValidDate(dateOfBirth)) {
      setError('Please enter a valid date of birth.');
      return;
    }

    setSubmitting(true);
    try {
      const response = await fetch(`${API_BASE_URL}/patients`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${token}`,
        },
        body: JSON.stringify({ name: name.trim(), date_of_birth: dateOfBirth }),
      });

      if (!response.ok) {
        const data = await response.json().catch(() => ({}));
        throw new Error(data.detail || 'Failed to create patient');
      }

      const created: Patient = await response.json();
      onCreated({ ...created, date_of_birth: dateOfBirth });
      onClose();
    } catch (err: any) {
      setError(err?.message || 'An error occurred');
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <div
      className="modal-backdrop"
      onClick={(e) => {
        if (e.target === e.currentTarget) onClose();
      }}
      role="dialog"
      aria-modal="true"
      aria-labelledby="add-patient-title"
    >
      <div className="modal">
        <div className="modal__header">
          <h3 id="add-patient-title" className="modal__title">Add patient</h3>
          <button
            type="button"
            className="modal__close"
            onClick={onClose}
            aria-label="Close dialog"
          >
            ✕
          </button>
        </div>

        <form className="stack" onSubmit={handleSubmit}>
          {error && <div className="alert alert--error">{error}</div>}

          <div className="field">
            <label className="field__label" htmlFor="patient-name">Full name</label>
            <input
              id="patient-name"
              className="input"
              type="text"
              value={name}
              onChange={(e) => setName(e.target.value)}
              placeholder="e.g. Jane Doe"
              autoFocus
              required
            />
          </div>

          <div className="field">
            <label className="field__label" htmlFor="patient-dob">Date of birth</label>
            <input
              id="patient-dob"
              className="input"
              type="date"
              value={dateOfBirth}
              onChange={(e) => setDateOfBirth(e.target.value)}
              max={new Date().toISOString().slice(0, 10)}
              required
            />
          </div>

          <div className="modal__actions">
            <button
              type="button"
              className="btn"
              onClick={onClose}
              disabled={submitting}
            >
              Cancel
            </button>
            <button
              type="submit"
              className="btn btn--primary"
              disabled={submitting}
            >
              {submitting ? 'Saving…' : 'Add patient'}
            </button>
          </div>
        </form>
      </div>
    </div>
  );
}

function PatientCard({ patient }: { patient: Patient }) {
  const initials = useMemo(() => getInitials(patient.name), [patient.name]);
  const dobLabel = useMemo(() => formatDate(patient.date_of_birth), [patient.date_of_birth]);
  const age = useMemo(() => computeAge(patient.date_of_birth), [patient.date_of_birth]);

  return (
    <article className="patient-card">
      <div className="patient-card__top">
        <div className="patient-card__avatar" aria-hidden="true">{initials}</div>
        <div>
          <div className="patient-card__name">{patient.name}</div>
          <div className="patient-card__id">Patient #{patient.id}</div>
        </div>
      </div>

      <div className="patient-card__meta" title="Date of birth">
        <svg
          className="patient-card__meta-icon"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          strokeWidth="2"
          strokeLinecap="round"
          strokeLinejoin="round"
          aria-hidden="true"
        >
          <rect x="3" y="4" width="18" height="18" rx="2" ry="2" />
          <line x1="16" y1="2" x2="16" y2="6" />
          <line x1="8" y1="2" x2="8" y2="6" />
          <line x1="3" y1="10" x2="21" y2="10" />
        </svg>
        <span>Born {dobLabel || '—'}</span>
      </div>

      {age !== null && (
        <span className="patient-card__age-pill">{age} years old</span>
      )}
    </article>
  );
}

function PatientsSkeleton() {
  return (
    <div className="patients-grid" aria-busy="true" aria-live="polite">
      {[0, 1, 2].map((i) => (
        <div key={i} className="patient-card">
          <div className="skeleton" style={{ height: 44, width: 44, borderRadius: '50%' }} />
          <div className="skeleton" style={{ height: 16, width: '60%' }} />
          <div className="skeleton" style={{ height: 12, width: '40%' }} />
        </div>
      ))}
    </div>
  );
}

function EmptyState({ onAdd }: { onAdd: () => void }) {
  return (
    <div className="empty-state">
      <div className="empty-state__icon" aria-hidden="true">＋</div>
      <div className="empty-state__title">No patients yet</div>
      <p className="empty-state__hint">
        Get started by adding your first patient. Their name and date of birth will be stored securely.
      </p>
      <button type="button" className="btn btn--primary" onClick={onAdd}>
        Add your first patient
      </button>
    </div>
  );
}

export function PatientsList({ onAddRequest }: { onAddRequest?: () => void }) {
  const [patients, setPatients] = useState<Patient[]>([]);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const { token } = useAuth();

  const loadPatients = async () => {
    setLoading(true);
    setError(null);
    try {
      const response = await fetch(`${API_BASE_URL}/patients`, {
        headers: {
          'Content-Type': 'application/json',
          'Authorization': `Bearer ${token}`,
        },
      });
      if (!response.ok) {
        throw new Error('Failed to fetch patients');
      }
      const data: Patient[] = await response.json();
      setPatients(data);
    } catch (err: any) {
      setError(err?.message || 'An error occurred');
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    if (!token) {
      setPatients([]);
      setLoading(false);
      return;
    }
    loadPatients();
  }, [token]);

  const handleCreated = (created: Patient) => {
    setPatients((current) => {
      // Avoid duplicates if the server already returned the created row.
      const without = current.filter((p) => String(p.id) !== String(created.id));
      return [...without, created];
    });
  };

  if (loading) return <PatientsSkeleton />;

  if (error) {
    return (
      <div className="stack">
        <div className="alert alert--error">{error}</div>
        <div>
          <button type="button" className="btn" onClick={loadPatients}>
            Try again
          </button>
        </div>
      </div>
    );
  }

  if (patients.length === 0) {
    return <EmptyState onAdd={() => onAddRequest?.()} />;
  }

  return (
    <div className="stack">
      <div className="patients-grid">
        {patients.map((patient) => (
          <PatientCard key={patient.id} patient={patient} />
        ))}
      </div>
    </div>
  );
}
