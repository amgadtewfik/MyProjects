"""Comprehensive unit tests for authentication module."""

import pytest
from datetime import datetime, timedelta
from jose import jwt
import bcrypt


SECRET_KEY = "your-super-secret-key-change-in-production"
ALGORITHM = "HS256"
ACCESS_TOKEN_EXPIRE_MINUTES = 30


def test_verify_password():
    """Test password verification with correct and incorrect passwords."""
    # Generate a hash
    salt = bcrypt.gensalt()
    hashed = bcrypt.hashpw(b"testpassword", salt)

    # Correct password should return True
    assert bcrypt.checkpw(b"testpassword", hashed) is True

    # Incorrect password should return False
    assert bcrypt.checkpw(b"wrongpassword", hashed) is False


def test_verify_password_empty():
    """Test with empty passwords."""
    salt = bcrypt.gensalt()
    hashed = bcrypt.hashpw(b"", salt)

    assert bcrypt.checkpw(b"", hashed) is True
    assert bcrypt.checkpw(b"anything", hashed) is False


def test_get_password_hash_uniqueness():
    """Test that each hash call produces a unique hash."""
    from auth import get_password_hash

    hash1 = get_password_hash("testpassword")
    hash2 = get_password_hash("testpassword")

    # Hashes should be different due to random salt
    assert hash1 != hash2


def test_get_password_hash_verification():
    """Test that generated hashes can verify the original password."""
    from auth import get_password_hash, verify_password

    password = "SecurePass1"
    hashed = get_password_hash(password)

    assert verify_password(password, hashed) is True
    assert verify_password("WrongPass", hashed) is False


def test_create_access_token():
    """Test JWT token creation."""
    from auth import create_access_token

    data = {"sub": "test@example.com"}
    token = create_access_token(data)

    # Decode and verify
    payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
    assert payload["sub"] == "test@example.com"
    assert "exp" in payload


def test_create_access_token_expiration():
    """Test that tokens have correct expiration time."""
    from auth import create_access_token

    data = {"sub": "user@test.com"}
    token = create_access_token(data)

    payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
    exp = datetime.utcfromtimestamp(payload["exp"])
    now = datetime.utcnow()

    # Expiration should be approximately ACCESS_TOKEN_EXPIRE_MINUTES from now
    expected_delta = timedelta(minutes=ACCESS_TOKEN_EXPIRE_MINUTES)
    actual_delta = exp - now
    assert abs((actual_delta - expected_delta).total_seconds()) < 2


def test_create_access_token_copies_data():
    """Test that original data dict is not modified."""
    from auth import create_access_token

    data = {"sub": "test@example.com"}
    token = create_access_token(data)

    # Original should only have 'sub', not 'exp'
    assert "exp" not in data
    assert data["sub"] == "test@example.com"


def test_create_invalid_jwt():
    """Test creating a JWT with invalid algorithm."""
    from auth import create_access_token

    data = {"sub": "test@example.com"}
    token = create_access_token(data)

    # Try to decode with wrong algorithm
    with pytest.raises(Exception):
        jwt.decode(token, SECRET_KEY, algorithms=["RS256"])


def test_decode_expired_token():
    """Test decoding an expired token."""
    from auth import create_access_token

    data = {"sub": "test@example.com"}
    # Manually create a token with past expiration
    to_encode = data.copy()
    expire = datetime.utcnow() - timedelta(minutes=1)  # Expired 1 minute ago
    to_encode.update({"exp": expire})
    expired_token = jwt.encode(to_encode, SECRET_KEY, algorithm=ALGORITHM)

    with pytest.raises(Exception):
        jwt.decode(expired_token, SECRET_KEY, algorithms=[ALGORITHM])


def test_decode_invalid_signature():
    """Test decoding a token with wrong secret key."""
    from auth import create_access_token

    data = {"sub": "test@example.com"}
    token = create_access_token(data)

    # Try to decode with wrong secret
    with pytest.raises(Exception):
        jwt.decode(token, "wrong-secret", algorithms=[ALGORITHM])


def test_decode_missing_sub():
    """Test decoding a token without 'sub' claim."""
    from auth import create_access_token

    data = {"other": "value"}  # No 'sub' field
    token = create_access_token(data)

    payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])
    assert payload.get("sub") is None


def test_verify_password_edge_cases():
    """Test password verification with edge cases."""
    from auth import get_password_hash, verify_password

    # Very long password
    long_pass = "A" * 1000 + "1"
    hashed = get_password_hash(long_pass)
    assert verify_password(long_pass, hashed) is True
    assert verify_password("wrong", hashed) is False

    # Unicode characters in password
    unicode_pass = "pässwörd_123"
    hashed = get_password_hash(unicode_pass)
    assert verify_password(unicode_pass, hashed) is True

    # Password with special characters
    special_pass = "!@#$%^&*()_+-=[]{}|;':\",./<>?1"
    hashed = get_password_hash(special_pass)
    assert verify_password(special_pass, hashed) is True


def test_get_password_hash_consistency():
    """Test that the same password always verifies correctly."""
    from auth import get_password_hash, verify_password

    password = "ConsistentPass1"
    for i in range(5):
        hashed = get_password_hash(password)
        assert verify_password(password, hashed) is True


def test_create_access_token_with_complex_data():
    """Test token creation with complex nested data."""
    from auth import create_access_token

    data = {
        "sub": "user@example.com",
        "role": "admin",
        "permissions": ["read", "write", "delete"],
    }
    token = create_access_token(data)
    payload = jwt.decode(token, SECRET_KEY, algorithms=[ALGORITHM])

    assert payload["sub"] == "user@example.com"
    assert payload["role"] == "admin"
    assert payload["permissions"] == ["read", "write", "delete"]


def test_secret_key_configuration():
    """Test that the secret key is properly configured."""
    from auth import SECRET_KEY

    assert SECRET_KEY is not None
    assert len(SECRET_KEY) > 0
    assert SECRET_KEY != ""  # Not empty
