"""Doc-coverage lint (rules.md R9.1): EVERY drivers/<name>/ and modules/<name>/ that ships a
manifest.json MUST have a reference doc at
documentation/uk/03-framework-reference/{drivers,modules}/<name>.md.

Keeps the docs LIVING: a new driver/module added without its doc fails the host suite, so the
per-driver / per-module documentation set can never silently drift behind the code. (uk is the
authoritative tree; the en mirror is checked separately / by review.)
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DOCS = ROOT / "documentation" / "uk" / "03-framework-reference"


def _component_names(kind: str):
    """drivers|modules folder names that ship a manifest.json (i.e. real components)."""
    base = ROOT / kind
    if not base.is_dir():
        return []
    return sorted(p.name for p in base.iterdir()
                  if p.is_dir() and (p / "manifest.json").exists())


def _documented(kind_dir: str):
    d = DOCS / kind_dir
    return {p.stem for p in d.glob("*.md")} if d.is_dir() else set()


def test_every_driver_has_a_reference_doc():
    documented = _documented("drivers")
    missing = [n for n in _component_names("drivers") if n not in documented]
    assert not missing, (
        f"drivers without a reference doc (rules.md R9.1): {missing}. "
        f"Add documentation/uk/03-framework-reference/drivers/<name>.md for each.")


def test_every_module_has_a_reference_doc():
    documented = _documented("modules")
    missing = [n for n in _component_names("modules") if n not in documented]
    assert not missing, (
        f"modules without a reference doc (rules.md R9.1): {missing}. "
        f"Add documentation/uk/03-framework-reference/modules/<name>.md for each.")
