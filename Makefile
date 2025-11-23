.PHONY: wheel develop test format clean

wheel:
	uv build --wheel

develop: wheel
	uv pip install -e . --force-reinstall
	@echo "Copying .pyi stub file..."
	@cp _build/cp*/_webcodecs_py.pyi src/webcodecs/ 2>/dev/null || true

test: develop
	uv run pytest tests/ --timeout=10

format:
	clang-format -i src/bindings/*.cpp src/bindings/*.h
	uv run ruff format tests/

clean:
	rm -rf _build dist *.egg-info _deps
