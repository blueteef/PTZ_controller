"""
db/__init__.py — SQLite face recognition database.

Stores 128-d dlib face encodings with associated names.
Each enrolled person can have multiple encodings (one per enrollment session)
for better recognition accuracy.
"""

from __future__ import annotations

import logging
from typing import Optional

import numpy as np

from app import config

log = logging.getLogger(__name__)


def init_db() -> None:
    import sqlite3
    with sqlite3.connect(config.DB_PATH) as conn:
        conn.execute("""
            CREATE TABLE IF NOT EXISTS faces (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                name        TEXT    NOT NULL,
                encoding    BLOB    NOT NULL,
                created_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        conn.commit()
    log.info("Face DB initialized at %s", config.DB_PATH)


def enroll_face(name: str, encoding: np.ndarray) -> int:
    import sqlite3
    with sqlite3.connect(config.DB_PATH) as conn:
        cur = conn.execute(
            "INSERT INTO faces (name, encoding) VALUES (?, ?)",
            (name, encoding.tobytes())
        )
        conn.commit()
        return cur.lastrowid


def list_faces() -> list[dict]:
    import sqlite3
    with sqlite3.connect(config.DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            "SELECT id, name, created_at FROM faces ORDER BY name, id"
        ).fetchall()
        return [dict(r) for r in rows]


def delete_face(face_id: int) -> bool:
    import sqlite3
    with sqlite3.connect(config.DB_PATH) as conn:
        cur = conn.execute("DELETE FROM faces WHERE id = ?", (face_id,))
        conn.commit()
        return cur.rowcount > 0


def delete_face_by_name(name: str) -> int:
    """Delete all encodings for a given name. Returns number deleted."""
    import sqlite3
    with sqlite3.connect(config.DB_PATH) as conn:
        cur = conn.execute("DELETE FROM faces WHERE name = ?", (name,))
        conn.commit()
        return cur.rowcount


def get_all_encodings() -> list[tuple[str, np.ndarray]]:
    """Return list of (name, encoding_array) for every enrolled face."""
    import sqlite3
    with sqlite3.connect(config.DB_PATH) as conn:
        conn.row_factory = sqlite3.Row
        rows = conn.execute("SELECT name, encoding FROM faces").fetchall()
        return [
            (r["name"], np.frombuffer(r["encoding"], dtype=np.float64))
            for r in rows
        ]
