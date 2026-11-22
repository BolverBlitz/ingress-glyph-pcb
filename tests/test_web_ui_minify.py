"""Regression checks for readable WebUI sources and build-time minification."""

from pathlib import Path
import gzip
import json
import re
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest


PROJECT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_DIR / "scripts"))

from ui_translations import translate_asset
from web_ui_minify import minify_css, minify_html, minify_js


SCRIPTS = re.compile(r"<script\b[^>]*>(.*?)</script>", re.I | re.S)
NODE = shutil.which("node")


class MinificationTests(unittest.TestCase):
    def test_all_static_assets_have_deterministic_gzip_round_trip(self):
        class BuildEnv:
            def __init__(self, build_dir, language):
                self.build_dir = build_dir
                self.language = language

            def subst(self, value):
                return str(PROJECT_DIR if value == "$PROJECT_DIR" else self.build_dir)

            def GetProjectOption(self, name):
                if name != "custom_ui_language":
                    raise KeyError(name)
                return self.language

            def Append(self, **kwargs):
                pass

        assets = {
            "index.html": "MAIN_PAGE_GZIP", "draw.html": "DRAW_PAGE_GZIP",
            "settings.html": "SETTINGS_PAGE_GZIP", "help.html": "HELP_BUTTONS_PAGE_GZIP",
            "help_settings.html": "HELP_SETTINGS_PAGE_GZIP", "nearby.html": "NEARBY_PAGE_GZIP",
            "index.css": "MAIN_CSS_GZIP", "draw.css": "DRAW_CSS_GZIP",
            "settings.css": "SETTINGS_CSS_GZIP", "nearby.css": "NEARBY_CSS_GZIP",
            "flashlight.css": "FLASHLIGHT_CSS_GZIP", "flashlight.js": "FLASHLIGHT_JS_GZIP",
            "manifest.webmanifest": "PWA_MANIFEST_GZIP", "sw.js": "SERVICE_WORKER_GZIP",
            "icon.svg": "APP_ICON_GZIP",
        }
        self.assertEqual(set(assets), {path.name for path in (PROJECT_DIR / "WebUI").iterdir()})
        version = re.search(r'^#define FIRMWARE_VERSION "([^"]+)"',
                            (PROJECT_DIR / "src/config.h").read_text(encoding="utf-8"), re.M).group(1)
        (PROJECT_DIR / ".pio").mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=PROJECT_DIR / ".pio") as temp_root:
            for language in ("en", "de", "ja"):
                with self.subTest(language=language):
                    build_dir = Path(temp_root) / language
                    build_dir.mkdir()
                    runpy.run_path(
                        str(PROJECT_DIR / "scripts" / "generate_web_ui.py"),
                        init_globals={"Import": lambda *_: None, "env": BuildEnv(build_dir, language)},
                    )
                    generated = (build_dir / "localized_web_ui.h").read_text(encoding="utf-8")
                    arrays = {
                        name: bytes(int(byte, 16) for byte in values.split(",") if byte.strip())
                        for name, values in re.findall(
                            r"const uint8_t (\w+)\[\] PROGMEM = \{([^}]*)\};", generated
                        )
                    }
                    self.assertEqual(set(arrays), set(assets.values()))
                    for filename, constant in assets.items():
                        source = (PROJECT_DIR / "WebUI" / filename).read_text(encoding="utf-8")
                        localized = translate_asset(filename, source, language, version)
                        if filename.endswith(".html"):
                            minified = minify_html(localized)
                        elif filename.endswith(".css"):
                            minified = minify_css(localized)
                        elif filename.endswith(".js"):
                            minified = minify_js(localized)
                        elif filename == "manifest.webmanifest":
                            minified = json.dumps(json.loads(localized), separators=(",", ":"), ensure_ascii=False)
                        else:
                            minified = re.sub(r">\s+<", "><", localized).strip()
                        self.assertEqual(gzip.decompress(arrays[constant]).decode("utf-8"), minified)
                        self.assertLessEqual(len(arrays[constant]), len(gzip.compress(
                            minified.encode("utf-8"), compresslevel=9, mtime=0)))
                    first = (build_dir / "localized_web_ui.h").read_bytes()
                    modified = (build_dir / "localized_web_ui.h").stat().st_mtime_ns
                    runpy.run_path(
                        str(PROJECT_DIR / "scripts" / "generate_web_ui.py"),
                        init_globals={"Import": lambda *_: None, "env": BuildEnv(build_dir, language)},
                    )
                    self.assertEqual(first, (build_dir / "localized_web_ui.h").read_bytes())
                    self.assertEqual(modified, (build_dir / "localized_web_ui.h").stat().st_mtime_ns)

    def test_line_comments_are_removed(self):
        source = "const first = 1; // Explanation\nconst second = 2;"
        result = minify_js(source)
        self.assertNotIn("Explanation", result)
        self.assertIn("second", result)

    def test_html_script_attributes_and_preformatted_text(self):
        source = (
            '<pre>first\n  second</pre><script defer type="application/javascript">'
            'const first = 1; // Explanation\nconst second = 2;</script>'
        )
        result = minify_html(source)
        self.assertIn("first\n  second", result)
        self.assertNotIn("Explanation", result)
        self.assertIn("second", SCRIPTS.findall(result)[0])

    def test_css_comments_are_removed(self):
        result = minify_css("/* Explanation */ .card { width: calc(100% - 2px); }")
        self.assertNotIn("Explanation", result)
        self.assertIn("calc(100% - 2px)", result)

    @unittest.skipUnless(NODE, "Node.js is needed only for JavaScript verification")
    def test_javascript_behavior(self):
        cases = {
            "comments_and_strings": r'''
                const address = "https://example.org/*literal*/";
                // This comment must not consume the following statement.
                const pattern = /https?:\/\//;
                function result() { return [address, pattern.test(address)]; }
            ''',
            "nested_templates": r'''
                const label = "Agent";
                function result() { return `Hello ${`${label} ${1 + 2}`}!`; }
            ''',
            "automatic_semicolon_insertion": '''
                function nothing() { return
                    { ignored: true };
                }
                let count = 1;
                count
                ++count;
                function result() { return [nothing() === undefined, count]; }
            ''',
            "operators_and_optional_chaining": '''
                let value = 2;
                const extra = value + +value;
                const object = { nested: { value: 3 } };
                function result() { return [extra, object?.nested?.value ?? 0]; }
            ''',
        }
        for name, source in cases.items():
            with self.subTest(name=name):
                outputs = []
                for candidate in (source, minify_js(source)):
                    executed = subprocess.run(
                        [NODE, "-"],
                        input=candidate + "\nconsole.log(JSON.stringify(result()));",
                        encoding="utf-8", capture_output=True, check=True,
                    )
                    outputs.append(executed.stdout)
                self.assertEqual(outputs[0], outputs[1])

    @unittest.skipUnless(NODE, "Node.js is needed only for JavaScript verification")
    def test_all_localized_assets_parse(self):
        for path in sorted((PROJECT_DIR / "WebUI").iterdir()):
            if path.suffix not in (".html", ".js"):
                continue
            source = path.read_text(encoding="utf-8")
            for language in ("en", "de", "ja"):
                with self.subTest(asset=path.name, language=language):
                    localized = translate_asset(path.name, source, language, "test")
                    if path.suffix == ".html":
                        original = SCRIPTS.findall(localized)
                        minified = SCRIPTS.findall(minify_html(localized))
                        self.assertEqual(len(original), len(minified))
                        scripts = original + minified
                    else:
                        scripts = [localized, minify_js(localized)]
                    for script in scripts:
                        subprocess.run(
                            [NODE, "--check", "-"], input=script,
                            encoding="utf-8", capture_output=True, check=True,
                        )


if __name__ == "__main__":
    unittest.main()
