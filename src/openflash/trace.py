from __future__ import annotations

import json
from pathlib import Path

from .model import IORequest, Operation


def load_jsonl_trace(path: str | Path) -> list[IORequest]:
    """Load one request per line with operation, lpn, pages, and arrival_us fields."""
    requests: list[IORequest] = []
    with Path(path).open(encoding="utf-8") as trace:
        for line_number, line in enumerate(trace, start=1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            try:
                value = json.loads(line)
                requests.append(
                    IORequest(
                        request_id=int(value.get("request_id", len(requests))),
                        operation=Operation(value["operation"]),
                        lpn=int(value["lpn"]),
                        pages=int(value.get("pages", 1)),
                        arrival_us=float(value.get("arrival_us", 0.0)),
                    )
                )
            except (KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
                raise ValueError(f"invalid trace record at {path}:{line_number}: {error}") from error
    if not requests:
        raise ValueError(f"trace contains no requests: {path}")
    return requests
