import sqlite3
from fastapi import FastAPI, Depends, HTTPException, status
from fastapi.security import OAuth2PasswordBearer, OAuth2PasswordRequestForm
from pydantic import BaseModel, EmailStr
import uvicorn
import uuid

from auth import create_access_token, verify_password, get_password_hash, get_current_user

app = FastAPI(title="Python Auth API")

# CORS setup
from fastapi.middleware.cors import CORSMiddleware
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:3000", "http://localhost:3001"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

DATABASE_FILE = "users.db"

def init_db():
    print("Initializing database...")
    conn = sqlite3.connect(DATABASE_FILE)
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS users (
            id TEXT PRIMARY KEY,
            email TEXT UNIQUE NOT NULL,
            hashed_password TEXT NOT NULL
        )
    ''')
    cursor.execute('''
     create table if not exists patients (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,         
            date_of_birth TEXT NOT NULL 
        )
    ''')
    print("Database initialized.")
    conn.commit()
    conn.close()

init_db()

class Patient(BaseModel):
    name: str
    date_of_birth: str

class UserCreate(BaseModel):
    email: EmailStr
    password: str

class UserResponse(BaseModel):
    email: str
    id: str

class PatientResponse(BaseModel):
    id: int
    name: str
    date_of_birth: str  

class Token(BaseModel):
    access_token: str
    token_type: str

def get_db_connection():
    conn = sqlite3.connect(DATABASE_FILE)
    conn.row_factory = sqlite3.Row
    return conn

@app.post("/auth/register", response_model=UserResponse)
async def register(user_data: UserCreate):
    conn = get_db_connection()
    cursor = conn.cursor()
    
    try:
        cursor.execute('SELECT email FROM users WHERE email = ?', (user_data.email,))
        if cursor.fetchone():
            conn.close()
            raise HTTPException(status_code=400, detail="Email already registered")
        
        # Password validation
        if (len(user_data.password) < 8 or
            not any(char.isdigit() for char in user_data.password) or
            not any(char.isalpha() for char in user_data.password)):
            conn.close()
            raise HTTPException(status_code=400, detail="Password must be at least 8 characters long and contain both letters and numbers")
        
        hashed_password = get_password_hash(user_data.password)
        hashed_password_str = hashed_password.decode('utf-8')
        
        user_id = f"user_{uuid.uuid4().hex[:8]}"
        
        cursor.execute(
            'INSERT INTO users (id, email, hashed_password) VALUES (?, ?, ?)',
            (user_id, user_data.email, hashed_password_str)
        )
        conn.commit()
        conn.close()
        
        return {"email": user_data.email, "id": user_id}
    except HTTPException:
        raise
    except Exception as e:
        conn.close()
        raise HTTPException(status_code=500, detail=str(e))

@app.post("/auth/login", response_model=Token)
async def login(form_data: OAuth2PasswordRequestForm = Depends()):
    conn = get_db_connection()
    cursor = conn.cursor()
    
    cursor.execute('SELECT id, email, hashed_password FROM users WHERE email = ?', (form_data.username,))
    user_row = cursor.fetchone()
    conn.close()
    
    if not user_row:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Incorrect email or password",
            headers={"WWW-Authenticate": "Bearer"},
        )
    
    user_id = user_row['id']
    user_email = user_row['email']
    hashed_password = user_row['hashed_password'].encode('utf-8')
    
    if not verify_password(form_data.password, hashed_password):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Incorrect email or password",
            headers={"WWW-Authenticate": "Bearer"},
        )
    
    access_token = create_access_token(data={"sub": user_email})
    return {"access_token": access_token, "token_type": "bearer"}

@app.get("/auth/me", response_model=UserResponse)
async def get_me(current_user: dict = Depends(get_current_user)):
    conn = get_db_connection()
    cursor = conn.cursor()
    
    cursor.execute('SELECT id, email FROM users WHERE email = ?', (current_user["sub"],))
    user_row = cursor.fetchone()
    conn.close()
    
    if not user_row:
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="User not found",
        )
        
    return {"email": user_row['email'], "id": user_row['id']}

#post endpoint to create a patient, requires authentication
@app.post("/patients", response_model=  PatientResponse)
async def create_patient(patient_data: Patient):
    conn = get_db_connection()
    cursor = conn.cursor()
    print(f"Received patient data: {patient_data}")
    try:
        cursor.execute('SELECT name FROM patients WHERE name = ?', (patient_data.name,))
        if cursor.fetchone():
            conn.close()
            raise HTTPException(status_code=400, detail="Patient with this name already exists")
        
        print(f"Inserting patient: {patient_data.name}, DOB: {patient_data.date_of_birth}")
        cursor.execute(
            'INSERT INTO patients (name, date_of_birth) VALUES (?, ?)',
            (patient_data.name, patient_data.date_of_birth)
        )
        print(f"Inserted patient: {patient_data.name}, DOB: {patient_data.date_of_birth}")
        conn.commit()
        conn.close()
        
        return {"name": patient_data.name, "date_of_birth": patient_data.date_of_birth}
    except HTTPException:
        raise
    except Exception as e:
        conn.close()
        raise HTTPException(status_code=500, detail=str(e))

#get endpoint to retrieve patients, requires authentication 
@app.get("/patients", response_model= list[PatientResponse])
async def get_patients(current_user: dict = Depends(get_current_user)):
    conn = get_db_connection()
    cursor = conn.cursor()

    cursor.execute('SELECT id, name, date_of_birth FROM patients')
    patients = cursor.fetchall()
    conn.close()
    return [{"id": patient["id"],   "name": patient["name"], "date_of_birth": patient["date_of_birth"]} for patient in patients]


if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=True)
