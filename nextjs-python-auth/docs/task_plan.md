# Task Plan: Comprehensive Unit Tests

## Project Overview

- **Backend**: Python FastAPI app with SQLite, JWT auth, bcrypt password hashing
  - Endpoints: `POST /auth/register`, `POST /auth/login`, `GET /auth/me`, `POST /patients`, `GET /patients`
  - Helpers in `auth.py`: `verify_password`, `get_password_hash`, `create_access_token`, `get_current_user`
- **Frontend**: Next.js 14 + React 18 + TypeScript
  - `AuthContext` (login, register, logout, token restore)
  - `Patients` components (`AddPatientModal`, `PatientsList`, helpers `getInitials`, `formatDate`, `computeAge`, `isValidDate`)
  - `login/page.tsx`, `dashboard/page.tsx`, `page.tsx` (root redirect)

## Test Layers

### Backend (Python, `pytest` + `httpx.AsyncClient`)

1. **Auth helpers (`auth.py`)**
   - `verify_password`: correct/incorrect password, bytes vs str input, empty strings, unicode, long passwords
   - `get_password_hash`: returns bytes, salts differ, bcrypt verification
   - `create_access_token`: decodable, contains `sub` & `exp`, expiration in 30 min
   - `get_current_user`: valid token → email; missing token → 401; malformed token → 401; wrong alg → 401; expired → 401

2. **Database init**
   - Creates `users` & `patients` tables; idempotent (running twice is safe)

3. **`POST /auth/register`**
   - Happy path
   - Duplicate email → 400
   - Invalid email → 422
   - Password < 8 chars → 400
   - Password without digit → 400
   - Password without letter → 400
   - Password only digits / only letters → 400
   - Missing fields → 422
   - Server-side unknown error → 500

4. **`POST /auth/login`**
   - Happy path returns token + bearer type
   - Unknown email → 401
   - Wrong password → 401
   - Empty fields → 400

5. **`GET /auth/me`**
   - With valid token → returns user
   - Missing token → 401
   - Invalid token → 401
   - Expired token → 401
   - Token with deleted user → 401

6. **`POST /patients`**
   - Happy path (returns id assigned)
   - Duplicate name → 400
   - Empty name → 422
   - Missing field → 422

7. **`GET /patients`**
   - Empty list
   - Multiple patients
   - Without token → 401
   - Invalid token → 401

### Frontend (TypeScript, `vitest` + `jsdom` + `@testing-library/react`)

1. **AuthContext**
   - Initial loading state, no token
   - Restores session from localStorage (valid token → user fetched)
   - Restores session with invalid token → token cleared, user null
   - `login` success → fetches user, stores token, calls API correctly
   - `login` failure → throws with API error detail
   - `register` success → POST JSON
   - `register` failure → throws
   - `logout` clears state and storage
   - `useAuth` outside provider throws

2. **`AddPatientModal`**
   - Doesn't render when `open=false`
   - Renders form when open
   - Submit empty name → error
   - Submit invalid date → error
   - Submit valid data → POST with bearer, calls onCreated, closes
   - Server 400 → error displayed
   - Server error without `detail` → fallback message
   - Escape key closes modal
   - Click on backdrop closes modal
   - Click inside modal does NOT close
   - Cancel button closes
   - Closes on open=false (resets state)

3. **`PatientsList`**
   - Loading skeleton
   - Empty state with onAdd callback
   - Renders cards for each patient
   - Handles 401 → error + retry button
   - Handles generic fetch error
   - Adds newly created patient (avoiding duplicates)
   - With no token → empty state, no fetch

4. **Pure helpers (`getInitials`, `formatDate`, `computeAge`, `isValidDate`)**
   - Tested directly via import

5. **Pages** (`LoginPage`, `DashboardPage`, `HomePage`)
   - Redirects when no user/token
   - Form validation
   - Submit login/register flows
   - Logout button
   - Add patient button shows modal

## File Layout

```
backend/
  tests/
    conftest.py            # shared fixtures (TestClient, db reset, app)
    test_auth_helpers.py
    test_register.py
    test_login.py
    test_me.py
    test_patients.py
    test_init_db.py
frontend/
  vitest.config.ts
  src/test-setup.ts
  tests/
    AuthContext.test.tsx
    AddPatientModal.test.tsx
    PatientsList.test.tsx
    helpers.test.ts
    LoginPage.test.tsx
    DashboardPage.test.tsx
    HomePage.test.tsx
docs/
  task_plan.md
  test_summary.md
```

## Verification Steps

1. `pytest backend/tests -v` → all pass
2. `cd frontend && yarn vitest run` → all pass
