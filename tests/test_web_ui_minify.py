"""Regression checks for readable WebUI sources and build-time minification."""

from pathlib import Path
import re
import shutil
import subprocess
import sys
import unittest


PROJECT_DIR = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PROJECT_DIR / "scripts"))

from ui_translations import translate_asset
from web_ui_minify import minify_css, minify_html, minify_js


SCRIPTS = re.compile(r"<script\b[^>]*>(.*?)</script>", re.I | re.S)
NODE = shutil.which("node")


class MinificationTests(unittest.TestCase):
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
