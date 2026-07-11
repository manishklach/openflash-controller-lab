import json

import pytest

from openflash.model import Operation
from openflash.trace import load_jsonl_trace


def test_jsonl_trace_loader(tmp_path) -> None:
    path = tmp_path / "trace.jsonl"
    path.write_text(json.dumps({"operation": "read", "lpn": 42, "arrival_us": 1.5}) + "\n")
    requests = load_jsonl_trace(path)
    assert requests[0].operation == Operation.READ
    assert requests[0].lpn == 42


def test_invalid_trace_reports_line(tmp_path) -> None:
    path = tmp_path / "bad.jsonl"
    path.write_text("{}\n")
    with pytest.raises(ValueError, match=r":1:"):
        load_jsonl_trace(path)
