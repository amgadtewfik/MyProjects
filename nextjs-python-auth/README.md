# Next.js + Python FastAPI Authentication Project

A full-stack application with a TypeScript React frontend (Next.js) and a Python API backend (FastAPI) for user authentication.

## Project Structure

```
nextjs-python-auth/
├── backend/                 # Python FastAPI backend
│   ├── main.py              # FastAPI app entry point with auth endpoints
│   ├── auth.py              # Authentication logic (JWT, password hashing)
│   └── requirements.txt     # Python dependencies
├── frontend/                # Next.js TypeScript React frontend
│   ├── package.json
│   ├── tsconfig.json
│   ├── next.config.ts
│   ├── app/
│   │   ├── layout.tsx
│   │   ├── page.tsx
│   │   ├── globals.css
│   │   ├── login/
│   │   │   └── page.tsx
│   │   └── dashboard/
│   │       └── page.tsx
│   └── components/
│       └── AuthContext.tsx  # Authentication context and state management
└── README.md
```

## Backend Setup (Python FastAPI)

1. Navigate to the backend directory:
   ```bash
   cd nextjs-python-auth/backend
   ```

2. Create a virtual environment and install dependencies:
   ```bash
   python3 -m venv venv
   source venv/bin/activate  # On Windows: venv\Scripts\activate
   pip install -r requirements.txt
   ```

3. Start the FastAPI server:
   ```bash
   python main.py
   ```

The backend API will be available at `http://localhost:8000`. API documentation is available at `http://localhost:8000/docs`.

## Frontend Setup (Next.js + TypeScript)

1. Navigate to the frontend directory:
   ```bash
   cd nextjs-python-auth/frontend
   ```

2. Install dependencies:
   ```bash
   npm install
   # or
   yarn install
   ```

3. Start the development server:
   ```bash
   npm run dev
   # or
   yarn dev
   ```

The frontend will be available at `http://localhost:3000`.

## Authentication Flow

1. **Registration**: Users can register by providing an email and password via the `/auth/register` endpoint.
2. **Login**: Users log in via the `/auth/login` endpoint using OAuth2 password grant flow (FormData with `username` and `password`).
3. **Token Storage**: On successful login, a JWT token is stored in `localStorage`.
4. **Protected Routes**: The dashboard requires a valid JWT token in the `Authorization: Bearer <token>` header.

## API Endpoints

### Backend (Python FastAPI)

- `POST /auth/register` - Register a new user
- `POST /auth/login` - Login and get JWT token
- `GET /auth/me` - Get current user info (protected, requires Bearer token)

### Frontend (Next.js)

- `/` - Redirects to login or dashboard based on authentication status
- `/login` - Login/Registration page
- `/dashboard` - Protected dashboard page showing user information

## Security Notes

- The `SECRET_KEY` in `backend/auth.py` should be changed to a secure random string in production.
- Passwords are hashed using bcrypt via the `passlib` library.
- JWT tokens expire after 30 minutes.
- In a production environment, replace the in-memory `users_db` with a proper database (e.g., PostgreSQL, MongoDB).
