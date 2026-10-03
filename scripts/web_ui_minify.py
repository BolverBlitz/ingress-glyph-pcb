"""Minify readable WebUI sources using a pinned Python build dependency."""

import importlib
from importlib import metadata
from functools import lru_cache
from pathlib import Path
import re
import subprocess
import sys


PROJECT_DIR = Path(__file__).resolve().parent.parent
DEPENDENCIES_DIR = (
    PROJECT_DIR / ".pio" / "web-ui-deps" / sys.implementation.cache_tag
)
REQUIREMENTS = PROJECT_DIR / "scripts" / "requirements-web-ui.txt"
MINIFIER_VERSION = "0.18.1"


@lru_cache(maxsize=1)
def minifier():
    # Use the same dependency for direct builds, flash helpers and release CI.
    # Install into the ignored project directory, not the user's global Python.
    sys.path.insert(0, str(DEPENDENCIES_DIR))
    try:
        installed = metadata.version("minify-html")
    except metadata.PackageNotFoundError:
        installed = None
    if installed != MINIFIER_VERSION:
        subprocess.check_call([
            sys.executable, "-m", "pip", "install",
            "--disable-pip-version-check", "--only-binary=:all:", "--upgrade",
            "--target", str(DEPENDENCIES_DIR), "-r", str(REQUIREMENTS),
        ])
        importlib.invalidate_caches()
    return importlib.import_module("minify_html")


def minify_js(source):
    # Reuse the HTML engine's JavaScript parser for external scripts as well.
    # Keep script-level bindings visible to inline HTML event handlers.
    if re.search(r"</script\b", source, re.I):
        raise ValueError("Escape closing script tags in JavaScript strings")
    wrapped = minifier().minify(
        "<script>" + source + "</script>",
        minify_js=True,
        keep_closing_tags=True,
    )
    if not wrapped.startswith("<script>") or not wrapped.endswith("</script>"):
        raise ValueError("Unexpected JavaScript minifier output")
    return wrapped[len("<script>"):-len("</script>")]


def minify_css(source):
    wrapped = minifier().minify(
        "<style>" + source + "</style>",
        minify_css=True,
        keep_closing_tags=True,
    )
    if not wrapped.startswith("<style>") or not wrapped.endswith("</style>"):
        raise ValueError("Unexpected CSS minifier output")
    return wrapped[len("<style>"):-len("</style>")]


def minify_html(source):
    return minifier().minify(
        source,
        minify_js=True,
        minify_css=True,
        keep_closing_tags=True,
        keep_html_and_head_opening_tags=True,
    )
