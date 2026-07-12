from __future__ import annotations

import json
import os
from pathlib import Path
from typing import Any


class CheckpointJournal:
    """Atomically replace a single FTL checkpoint for recovery experiments."""

    VERSION = 1

    def __init__(self, path: str | Path) -> None:
        self.path = Path(path)

    def write(self, checkpoint: dict[str, Any]) -> None:
        payload = {"version": self.VERSION, "checkpoint": checkpoint}
        temporary = self.path.with_suffix(self.path.suffix + ".tmp")
        temporary.write_text(json.dumps(payload, sort_keys=True, separators=(",", ":")))
        os.replace(temporary, self.path)

    def read(self) -> dict[str, Any]:
        payload = json.loads(self.path.read_text())
        if payload.get("version") != self.VERSION:
            raise ValueError("unsupported OpenFlash checkpoint version")
        checkpoint = payload.get("checkpoint")
        if not isinstance(checkpoint, dict):
            raise ValueError("invalid OpenFlash checkpoint")
        return checkpoint
