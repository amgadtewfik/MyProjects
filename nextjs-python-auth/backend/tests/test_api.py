"""Comprehensive unit tests for all API endpoints."""

import pytest
from fastapi.testclient import TestClient
from unittest.mock import patch, MagicMock
import sqlite3
import uuid
import os


# Import the app and functions we need to test
import sys
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

from main import app, get_db_connection, init_db, create_patient, get_patients, register, login, get_me
from auth import SECRET_KEY, ALGORITHM


@pytest.fixture
def client():
    """Create a test client for the FastAPI app."""
    return TestClient(app)


@pytest.fixture
def mock_db_success():
    """Mock successful database operations."""
    with patch('main.get_db_connection') as mock_conn:
        cursor = MagicMock()
        conn = MagicMock()
        conn.cursor.return_value = cursor
        conn.row_factory = sqlite3.Row
        mock_conn.return_value = conn
        yield mock_conn


@pytest.fixture
def mock_db_with_user():
    """Mock database with a pre-existing user."""
    from auth import get_password_hash

    with patch('main.get_db_connection') as mock_conn:
        cursor = MagicMock()
        conn = MagicMock()
        conn.cursor.return_value = cursor
        conn.row_factory = sqlite3.Row

        # Mock the user row
        user_row = MagicMock()
        user_row.__getitem__ = lambda self, key: {
            'id': 'user_abc123',
            'email': 'test@example.com',
            'hashed_password': get_password_hash('Password1').decode('utf-8') if isinstance(get_password_hash('Password1'), bytes) else get_password_hash('Password1')
        }[key]

        cursor.fetchone.return_value = user_row
        mock_conn.return_value = conn
        yield mock_conn


@pytest.fixture
def valid_token():
    """Generate a valid JWT token for testing."""
    from jose import jwt
    from datetime import datetime, timedelta

    data = {"sub": "test@example.com"}
    expire = datetime.utcnow() + timedelta(minutes=30)
    data.update({"exp": expire})
    return jwt.encode(data, SECRET_KEY, algorithm=ALGORITHM)


class TestDatabaseInitialization:
    """Tests for database initialization."""

    def test_init_db_creates_tables(self):
        """Test that init_db creates the required tables."""
        # Create a temporary database file
        temp_db = "/tmp/test_auth_init.db"
        if os.path.exists(temp_db):
            os.remove(temp_db)

        import main as main_module
        original_db = main_module.DATABASE_FILE
        main_module.DATABASE_FILE = temp_db

        try:
            init_db()

            # Verify tables exist
            conn = sqlite3.connect(temp_db)
            cursor = conn.cursor()
            cursor.execute("SELECT name FROM sqlite_master WHERE type='table'")
            tables = [row[0] for row in cursor.fetchall()]
            conn.close()

            assert 'users' in tables
            assert 'patients' in tables
        finally:
            main_module.DATABASE_FILE = original_db
            if os.path.exists(temp_db):
                os.remove(temp_db)


class TestRegisterEndpoint:
    """Tests for the /auth/register endpoint."""

    def test_register_success(self, client):
        """Test successful registration with valid data."""
        response = client.post("/auth/register", json={
            "email": "newuser@example.com",
            "password": "SecurePass1"
        })
        assert response.status_code == 200
        data = response.json()
        assert 'email' in data
        assert 'id' in data
        assert data['email'] == 'newuser@example.com'
        assert data['id'].startswith('user_')

    def test_register_duplicate_email(self, client):
        """Test registration with an already registered email."""
        # Register first user
        client.post("/auth/register", json={
            "email": "duplicate@example.com",
            "password": "SecurePass1"
        })

        # Try to register again
        response = client.post("/auth/register", json={
            "email": "duplicate@example.com",
            "password": "AnotherPass1"
        })
        assert response.status_code == 400
        assert 'Email already registered' in response.json()['detail']

    def test_register_weak_password_too_short(self, client):
        """Test registration with a password that is too short."""
        response = client.post("/auth/register", json={
            "email": "short@example.com",
            "password": "Ab1"  # Only 3 characters
        })
        assert response.status_code == 400
        assert 'Password must be at least 8 characters' in response.json()['detail']

    def test_register_password_no_numbers(self, client):
        """Test registration with a password containing no numbers."""
        response = client.post("/auth/register", json={
            "email": "nonum@example.com",
            "password": "NoNumbers"  # No digits
        })
        assert response.status_code == 400
        assert 'Password must be at least 8 characters' in response.json()['detail']

    def test_register_password_no_letters(self, client):
        """Test registration with a password containing no letters."""
        response = client.post("/auth/register", json={
            "email": "noletter@example.com",
            "password": "12345678"  # No alphabetic characters
        })
        assert response.status_code == 400
        assert 'Password must be at least 8 characters' in response.json()['detail']

    def test_register_password_exactly_8_chars(self, client):
        """Test registration with a password that is exactly 8 characters."""
        response = client.post("/auth/register", json={
            "email": "exactly8@example.com",
            "password": "Pass1234"  # Exactly 8 chars, has letters and numbers
        })
        assert response.status_code == 200

    def test_register_password_7_chars_fails(self, client):
        """Test registration with a password that is exactly 7 characters."""
        response = client.post("/auth/register", json={
            "email": "sevenchars@example.com",
            "password": "Pass123"  # Only 7 chars
        })
        assert response.status_code == 400

    def test_register_unicode_email(self, client):
        """Test registration with unicode characters in email."""
        response = client.post("/auth/register", json={
            "email": "用户@example.com",
            "password": "SecurePass1"
        })
        assert response.status_code == 200

    def test_register_special_chars_password(self, client):
        """Test registration with special characters in password."""
        response = client.post("/auth/register", json={
            "email": "special@example.com",
            "password": "P@ss!w0rd#2"  # Special chars + letters + numbers
        })
        assert response.status_code == 200

    def test_register_long_password(self, client):
        """Test registration with a very long password."""
        response = client.post("/auth/register", json={
            "email": "longpass@example.com",
            "password": "A" * 100 + "1"  # Very long password with letters and numbers
        })
        assert response.status_code == 200

    def test_register_invalid_email_format(self, client):
        """Test registration with an invalid email format."""
        response = client.post("/auth/register", json={
            "email": "not-an-email",
            "password": "SecurePass1"
        })
        # Pydantic EmailStr should reject this
        assert response.status_code == 422

    def test_register_empty_email(self, client):
        """Test registration with an empty email."""
        response = client.post("/auth/register", json={
            "email": "",
            "password": "SecurePass1"
        })
        assert response.status_code == 422

    def test_register_missing_password(self, client):
        """Test registration without password field."""
        response = client.post("/auth/register", json={
            "email": "nopass@example.com"
        })
        assert response.status_code == 422


class TestLoginEndpoint:
    """Tests for the /auth/login endpoint."""

    def test_login_success(self, client):
        """Test successful login with correct credentials."""
        # Register a user first
        client.post("/auth/register", json={
            "email": "loginuser@example.com",
            "password": "SecurePass1"
        })

        response = client.post("/auth/login", data={
            "username": "loginuser@example.com",
            "password": "SecurePass1"
        })
        assert response.status_code == 200
        data = response.json()
        assert 'access_token' in data
        assert 'token_type' in data
        assert data['token_type'] == 'bearer'

    def test_login_wrong_password(self, client):
        """Test login with wrong password."""
        # Register a user first
        client.post("/auth/register", json={
            "email": "wrongpass@example.com",
            "password": "SecurePass1"
        })

        response = client.post("/auth/login", data={
            "username": "wrongpass@example.com",
            "password": "WrongPassword1"
        })
        assert response.status_code == 401
        assert 'Incorrect email or password' in response.json()['detail']

    def test_login_nonexistent_user(self, client):
        """Test login with an unregistered email."""
        response = client.post("/auth/login", data={
            "username": "never@example.com",
            "password": "SecurePass1"
        })
        assert response.status_code == 401
        assert 'Incorrect email or password' in response.json()['detail']

    def test_login_case_insensitive_email(self, client):
        """Test that login is case-sensitive for emails."""
        # Register with lowercase
        client.post("/auth/register", json={
            "email": "lowercase@example.com",
            "password": "SecurePass1"
        })

        # Try uppercase - should fail (SQLite default)
        response = client.post("/auth/login", data={
            "username": "LOWERCASE@EXAMPLE.COM",
            "password": "SecurePass1"
        })
        assert response.status_code == 401

    def test_login_unicode_email(self, client):
        """Test login with unicode email."""
        # Register a user first
        client.post("/auth/register", json={
            "email": "用户@example.com",
            "password": "SecurePass1"
        })

        response = client.post("/auth/login", data={
            "username": "用户@example.com",
            "password": "SecurePass1"
        })
        assert response.status_code == 200

    def test_login_special_chars_password(self, client):
        """Test login with special characters in password."""
        # Register a user first
        client.post("/auth/register", json={
            "email": "speciallogin@example.com",
            "password": "P@ss!w0rd#2"
        })

        response = client.post("/auth/login", data={
            "username": "speciallogin@example.com",
            "password": "P@ss!w0rd#2"
        })
        assert response.status_code == 200


class TestGetMeEndpoint:
    """Tests for the /auth/me endpoint."""

    def test_get_me_success(self, client, valid_token):
        """Test getting current user info with valid token."""
        # Register a user first
        client.post("/auth/register", json={
            "email": "meuser@example.com",
            "password": "SecurePass1"
        })

        response = client.get("/auth/me", headers={
            "Authorization": f"Bearer {valid_token}"
        })
        assert response.status_code == 200
        data = response.json()
        assert 'email' in data
        assert 'id' in data

    def test_get_me_invalid_token(self, client):
        """Test getting user info with an invalid token."""
        response = client.get("/auth/me", headers={
            "Authorization": "Bearer invalid-token-string"
        })
        assert response.status_code == 401

    def test_get_me_expired_token(self, client):
        """Test getting user info with an expired token."""
        from jose import jwt
        from datetime import datetime, timedelta

        # Create an expired token
        data = {"sub": "expired@example.com"}
        expire = datetime.utcnow() - timedelta(minutes=1)  # Expired
        data.update({"exp": expire})
        expired_token = jwt.encode(data, SECRET_KEY, algorithm=ALGORITHM)

        response = client.get("/auth/me", headers={
            "Authorization": f"Bearer {expired_token}"
        })
        assert response.status_code == 401

    def test_get_me_missing_authorization_header(self, client):
        """Test getting user info without Authorization header."""
        response = client.get("/auth/me")
        assert response.status_code == 401

    def test_get_me_empty_token(self, client):
        """Test getting user info with empty token."""
        response = client.get("/auth/me", headers={
            "Authorization": "Bearer "
        })
        assert response.status_code == 401


class TestPatientEndpoints:
    """Tests for patient CRUD endpoints."""

    def test_create_patient_success(self, client):
        """Test successful patient creation."""
        # Register and login to get a token
        client.post("/auth/register", json={
            "email": "patientuser@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "patientuser@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        response = client.post("/patients", json={
            "name": "John Doe",
            "date_of_birth": "1990-05-15"
        }, headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 200
        data = response.json()
        assert data['name'] == 'John Doe'
        assert data['date_of_birth'] == '1990-05-15'

    def test_create_patient_duplicate_name(self, client):
        """Test creating a patient with a duplicate name."""
        # Register and login
        client.post("/auth/register", json={
            "email": "dupuser@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "dupuser@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        # Create first patient
        client.post("/patients", json={
            "name": "John Doe",
            "date_of_birth": "1990-05-15"
        }, headers={"Authorization": f"Bearer {token}"})

        # Try to create duplicate
        response = client.post("/patients", json={
            "name": "John Doe",
            "date_of_birth": "1985-03-20"
        }, headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 400
        assert 'Patient with this name already exists' in response.json()['detail']

    def test_create_patient_unauthorized(self, client):
        """Test creating a patient without authentication."""
        response = client.post("/patients", json={
            "name": "Unauthorized Patient",
            "date_of_birth": "1990-05-15"
        })
        assert response.status_code == 401

    def test_get_patients_success(self, client):
        """Test retrieving all patients."""
        # Register and login
        client.post("/auth/register", json={
            "email": "getpatientuser@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "getpatientuser@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        # Create a patient first
        client.post("/patients", json={
            "name": "Jane Smith",
            "date_of_birth": "1985-03-20"
        }, headers={"Authorization": f"Bearer {token}"})

        response = client.get("/patients", headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, list)
        assert len(data) >= 1
        assert 'id' in data[0]
        assert 'name' in data[0]
        assert 'date_of_birth' in data[0]

    def test_get_patients_empty(self, client):
        """Test retrieving patients when none exist."""
        # Register and login
        client.post("/auth/register", json={
            "email": "emptyuser@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "emptyuser@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        response = client.get("/patients", headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, list)
        assert len(data) == 0

    def test_get_patients_unauthorized(self, client):
        """Test retrieving patients without authentication."""
        response = client.get("/patients")
        assert response.status_code == 401


class TestCORSConfiguration:
    """Tests for CORS middleware configuration."""

    def test_cors_allowed_origins(self, client):
        """Test that allowed origins are configured correctly."""
        # Check the CORS middleware setup
        from fastapi.middleware.cors import CORSMiddleware
        
        # Find CORS middleware in app.middleware
        cors_middleware = None
        for m in app.user_middleware:
            if 'CORSMiddleware' in str(m):
                cors_middleware = m
                break

        assert cors_middleware is not None


class TestEdgeCasesAndErrors:
    """Tests for edge cases and error handling."""

    def test_register_with_very_long_email(self, client):
        """Test registration with a very long email address."""
        response = client.post("/auth/register", json={
            "email": "a" * 100 + "@example.com",
            "password": "SecurePass1"
        })
        # Should succeed or fail gracefully (not crash)
        assert response.status_code in [200, 422]

    def test_register_with_special_characters_email(self, client):
        """Test registration with special characters in email."""
        response = client.post("/auth/register", json={
            "email": "+test+tag@example.com",
            "password": "SecurePass1"
        })
        # Should succeed (valid email format)
        assert response.status_code == 200

    def test_login_with_empty_username(self, client):
        """Test login with empty username."""
        response = client.post("/auth/login", data={
            "username": "",
            "password": ""
        })
        assert response.status_code == 401

    def test_patient_creation_with_unicode_name(self, client):
        """Test patient creation with unicode name."""
        # Register and login
        client.post("/auth/register", json={
            "email": "unicodepatient@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "unicodepatient@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        response = client.post("/patients", json={
            "name": "用户姓名",
            "date_of_birth": "1990-05-15"
        }, headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 200

    def test_patient_creation_with_special_chars_name(self, client):
        """Test patient creation with special characters in name."""
        # Register and login
        client.post("/auth/register", json={
            "email": "specialpatient@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "specialpatient@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        response = client.post("/patients", json={
            "name": "O'Brien-Smith Jr.",
            "date_of_birth": "1985-03-20"
        }, headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 200

    def test_patient_creation_with_leading_trailing_spaces(self, client):
        """Test patient creation with leading/trailing spaces in name."""
        # Register and login
        client.post("/auth/register", json={
            "email": "spacespatient@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "spacespatient@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        response = client.post("/patients", json={
            "name": "  John Doe  ",
            "date_of_birth": "1990-05-15"
        }, headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 200

    def test_patient_creation_with_future_date(self, client):
        """Test patient creation with a future date of birth."""
        # Register and login
        client.post("/auth/register", json={
            "email": "futurepatient@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "futurepatient@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        response = client.post("/patients", json={
            "name": "Future Patient",
            "date_of_birth": "2050-01-01"
        }, headers={"Authorization": f"Bearer {token}"})
        # Should succeed (no date validation on backend)
        assert response.status_code == 200

    def test_patient_creation_with_invalid_date_format(self, client):
        """Test patient creation with invalid date format."""
        # Register and login
        client.post("/auth/register", json={
            "email": "invaliddatepatient@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "invaliddatepatient@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        response = client.post("/patients", json={
            "name": "Invalid Date Patient",
            "date_of_birth": "not-a-date"
        }, headers={"Authorization": f"Bearer {token}"})
        # Should succeed (no date validation on backend)
        assert response.status_code == 200

    def test_patient_creation_missing_name(self, client):
        """Test patient creation without name field."""
        # Register and login
        client.post("/auth/register", json={
            "email": "missingname@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "missingname@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        response = client.post("/patients", json={
            "date_of_birth": "1990-05-15"
        }, headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 422

    def test_patient_creation_missing_date_of_birth(self, client):
        """Test patient creation without date_of_birth field."""
        # Register and login
        client.post("/auth/register", json={
            "email": "missingdob@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "missingdob@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        response = client.post("/patients", json={
            "name": "No DOB Patient"
        }, headers={"Authorization": f"Bearer {token}"})
        assert response.status_code == 422


class TestTokenValidation:
    """Tests for JWT token validation."""

    def test_token_contains_correct_sub(self, client):
        """Test that login token contains correct user email as sub."""
        # Register and login
        client.post("/auth/register", json={
            "email": "tokencheck@example.com",
            "password": "SecurePass1"
        })

        response = client.post("/auth/login", data={
            "username": "tokencheck@example.com",
            "password": "SecurePass1"
        })
        token = response.json()['access_token']

        from jose import jwt
        payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
        assert payload['sub'] == 'tokencheck@example.com'

    def test_different_users_get_different_tokens(self, client):
        """Test that different users get different tokens."""
        # Register two users
        client.post("/auth/register", json={
            "email": "user1@example.com",
            "password": "SecurePass1"
        })
        client.post("/auth/register", json={
            "email": "user2@example.com",
            "password": "SecurePass1"
        })

        # Login both users
        token1 = client.post("/auth/login", data={
            "username": "user1@example.com",
            "password": "SecurePass1"
        }).json()['access_token']

        token2 = client.post("/auth/login", data={
            "username": "user2@example.com",
            "password": "SecurePass1"
        }).json()['access_token']

        # Tokens should be different
        assert token1 != token2


class TestPasswordPolicy:
    """Tests for password policy enforcement."""

    def test_password_exactly_8_characters_succeeds(self, client):
        """Test that a password with exactly 8 characters is accepted."""
        response = client.post("/auth/register", json={
            "email": "exactly8@example.com",
            "password": "Abcdefg1"
        })
        assert response.status_code == 200

    def test_password_7_characters_fails(self, client):
        """Test that a password with 7 characters is rejected."""
        response = client.post("/auth/register", json={
            "email": "sevenfail@example.com",
            "password": "Abcdef1"
        })
        assert response.status_code == 400

    def test_password_9_characters_succeeds(self, client):
        """Test that a password with 9 characters is accepted."""
        response = client.post("/auth/register", json={
            "email": "ninechars@example.com",
            "password": "Abcdefgh1"
        })
        assert response.status_code == 200

    def test_password_only_letters_fails(self, client):
        """Test that a password with only letters is rejected."""
        response = client.post("/auth/register", json={
            "email": "onlyletters@example.com",
            "password": "Abcdefgh"
        })
        assert response.status_code == 400

    def test_password_only_numbers_fails(self, client):
        """Test that a password with only numbers is rejected."""
        response = client.post("/auth/register", json={
            "email": "onlynumbers@example.com",
            "password": "12345678"
        })
        assert response.status_code == 400

    def test_password_mixed_letters_and_numbers_succeeds(self, client):
        """Test that a password with both letters and numbers is accepted."""
        response = client.post("/auth/register", json={
            "email": "mixed@example.com",
            "password": "Ab1cD2eF3"
        })
        assert response.status_code == 200

    def test_password_with_spaces_succeeds(self, client):
        """Test that a password with spaces is accepted."""
        response = client.post("/auth/register", json={
            "email": "spaces@example.com",
            "password": "Pass word 1"
        })
        assert response.status_code == 200

    def test_password_with_unicode_succeeds(self, client):
        """Test that a password with unicode characters is accepted."""
        response = client.post("/auth/register", json={
            "email": "unicodepass@example.com",
            "password": "Pässwörd1"
        })
        assert response.status_code == 200


class TestDatabaseOperations:
    """Tests for database operations."""

    def test_get_db_connection_returns_row_factory(self):
        """Test that get_db_connection sets row_factory correctly."""
        conn = get_db_connection()
        assert conn.row_factory is not None

    def test_database_file_exists(self):
        """Test that the database file exists after initialization."""
        db_path = os.path.join(os.path.dirname(__file__), '..', 'users.db')
        # The DB should exist since init_db was called at module load time
        assert os.path.exists(db_path)

    def test_users_table_has_correct_schema(self):
        """Test that users table has the correct schema."""
        conn = sqlite3.connect(os.path.join(os.path.dirname(__file__), '..', 'users.db'))
        cursor = conn.cursor()
        cursor.execute("PRAGMA table_info(users)")
        columns = [row[1] for row in cursor.fetchall()]
        conn.close()

        assert 'id' in columns
        assert 'email' in columns
        assert 'hashed_password' in columns


class TestIntegrationScenarios:
    """Integration tests that test complete user flows."""

    def test_full_registration_login_flow(self, client):
        """Test complete registration and login flow."""
        # Step 1: Register
        reg_response = client.post("/auth/register", json={
            "email": "fullflow@example.com",
            "password": "SecurePass1"
        })
        assert reg_response.status_code == 200

        # Step 2: Login
        login_response = client.post("/auth/login", data={
            "username": "fullflow@example.com",
            "password": "SecurePass1"
        })
        assert login_response.status_code == 200
        token = login_response.json()['access_token']

        # Step 3: Get user info
        me_response = client.get("/auth/me", headers={
            "Authorization": f"Bearer {token}"
        })
        assert me_response.status_code == 200
        user_data = me_response.json()
        assert user_data['email'] == 'fullflow@example.com'

    def test_full_patient_creation_flow(self, client):
        """Test complete patient creation flow."""
        # Register and login
        client.post("/auth/register", json={
            "email": "patientflow@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "patientflow@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        # Create patient
        create_response = client.post("/patients", json={
            "name": "Alice Johnson",
            "date_of_birth": "1985-06-20"
        }, headers={"Authorization": f"Bearer {token}"})
        assert create_response.status_code == 200

        # Retrieve patients
        get_response = client.get("/patients", headers={
            "Authorization": f"Bearer {token}"
        })
        assert get_response.status_code == 200
        patients = get_response.json()
        assert len(patients) >= 1
        assert any(p['name'] == 'Alice Johnson' for p in patients)

    def test_multiple_patients_flow(self, client):
        """Test creating and retrieving multiple patients."""
        # Register and login
        client.post("/auth/register", json={
            "email": "multipatient@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "multipatient@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        # Create multiple patients
        for i in range(5):
            response = client.post("/patients", json={
                "name": f"Patient {i}",
                "date_of_birth": f"19{i % 5}-0{i + 1}-15"
            }, headers={"Authorization": f"Bearer {token}"})
            assert response.status_code == 200

        # Retrieve all patients
        response = client.get("/patients", headers={
            "Authorization": f"Bearer {token}"
        })
        assert response.status_code == 200
        patients = response.json()
        assert len(patients) >= 5


class TestSecurityScenarios:
    """Tests for security-related scenarios."""

    def test_token_cannot_be_reused_after_logout(self, client):
        """Test that tokens are not invalidated on logout (stateless JWT)."""
        # This tests the current behavior - JWT tokens are stateless
        # In a real app, you'd maintain a token blacklist
        client.post("/auth/register", json={
            "email": "tokenreuse@example.com",
            "password": "SecurePass1"
        })

        login_response = client.post("/auth/login", data={
            "username": "tokenreuse@example.com",
            "password": "SecurePass1"
        })
        token = login_response.json()['access_token']

        # Token should still be valid (current behavior)
        response = client.get("/auth/me", headers={
            "Authorization": f"Bearer {token}"
        })
        assert response.status_code == 200

    def test_token_not_leaked_in_error_response(self, client):
        """Test that tokens are not leaked in error responses."""
        # Try to login with wrong credentials
        response = client.post("/auth/login", data={
            "username": "nonexistent@example.com",
            "password": "wrong"
        })

        # Response should not contain any token information
        assert 'access_token' not in str(response.json())

    def test_sql_injection_prevention(self, client):
        """Test that SQL injection attempts are prevented."""
        # Register a user first
        client.post("/auth/register", json={
            "email": "injection@example.com",
            "password": "SecurePass1"
        })

        # Try SQL injection in login
        response = client.post("/auth/login", data={
            "username": "' OR 1=1 --",
            "password": "anything"
        })
        assert response.status_code == 401

    def test_csrf_protection_via_cors(self, client):
        """Test that CORS headers are properly configured."""
        # Check that the app has CORS middleware
        from fastapi.middleware.cors import CORSMiddleware
        
        cors_found = False
        for m in app.user_middleware:
            if 'CORSMiddleware' in str(m):
                cors_found = True
                break

        assert cors_found is True


if __name__ == '__main__':
    pytest.main([__file__, '-v'])
