.PHONY: install test lint demo compare

install:
	python -m pip install -e ".[dev]"

test:
	python -m pytest -q

lint:
	python -m ruff check src tests

demo:
	python -m openflash.cli run --requests 10000 --policy read-priority

compare:
	python -m openflash.cli compare --requests 10000

