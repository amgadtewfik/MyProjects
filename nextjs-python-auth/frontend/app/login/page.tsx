'use client';

import { useState, FormEvent } from 'react';
import { useRouter } from 'next/navigation';
import { useAuth } from '@/components/AuthContext';

export default function LoginPage() {
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [error, setError] = useState('');
  const [isRegistering, setIsRegistering] = useState(false);
  const [submitting, setSubmitting] = useState(false);
  const { login, register } = useAuth();
  const router = useRouter();

  const handleSubmit = async (e: FormEvent) => {
    e.preventDefault();
    setError('');

    if (!email || !password) {
      setError('Please fill in both fields.');
      return;
    }

    setSubmitting(true);
    try {
      if (isRegistering) {
        await register(email, password);
        // After registering, switch to login mode so the user can sign in.
        setIsRegistering(false);
        setPassword('');
        setError('');
      } else {
        await login(email, password);
        router.push('/dashboard');
      }
    } catch (err: any) {
      setError(err?.message || 'An error occurred');
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <div className="auth-shell">
      <div className="auth-card">
        <div className="row" style={{ gap: 12, marginBottom: 16 }}>
          <span className="app-header__brand-mark" aria-hidden="true">P</span>
          <strong>Patient Portal</strong>
        </div>

        <h1 className="auth-card__title">
          {isRegistering ? 'Create an account' : 'Welcome back'}
        </h1>
        <p className="auth-card__subtitle">
          {isRegistering
            ? 'Sign up to start managing your patients.'
            : 'Sign in to access your dashboard.'}
        </p>

        <form className="stack" onSubmit={handleSubmit}>
          {error && <div className="alert alert--error">{error}</div>}

          <div className="field">
            <label className="field__label" htmlFor="email">Email</label>
            <input
              id="email"
              type="email"
              value={email}
              onChange={(e) => setEmail(e.target.value)}
              required
              autoComplete="email"
              className="input"
              placeholder="you@example.com"
            />
          </div>

          <div className="field">
            <label className="field__label" htmlFor="password">Password</label>
            <input
              id="password"
              type="password"
              value={password}
              onChange={(e) => setPassword(e.target.value)}
              required
              autoComplete={isRegistering ? 'new-password' : 'current-password'}
              minLength={8}
              className="input"
              placeholder="••••••••"
            />
            {isRegistering && (
              <span className="subtle">
                Must be at least 8 characters and include both letters and numbers.
              </span>
            )}
          </div>

          <button
            type="submit"
            className="btn btn--primary btn--block"
            disabled={submitting}
          >
            {submitting
              ? isRegistering ? 'Creating account…' : 'Signing in…'
              : isRegistering ? 'Create account' : 'Sign in'}
          </button>
        </form>

        <p className="auth-card__footer">
          {isRegistering ? 'Already have an account?' : "Don't have an account?"}{' '}
          <a
            href="#"
            onClick={(e) => {
              e.preventDefault();
              setIsRegistering(!isRegistering);
              setError('');
            }}
          >
            {isRegistering ? 'Sign in' : 'Create one'}
          </a>
        </p>
      </div>
    </div>
  );
}
