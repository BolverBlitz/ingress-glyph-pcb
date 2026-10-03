"""Compile and exercise the firmware's timing/protocol code on the host."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parent.parent


class FriendPairingTests(unittest.TestCase):
    def test_native_protocol_and_timing(self):
        with tempfile.TemporaryDirectory(prefix="glyph-pairing-") as temporary:
            directory = Path(temporary)
            source = PROJECT / "tests" / "friend_pairing_protocol_test.cpp"
            executable = directory / ("pairing.exe" if os.name == "nt" else "pairing")
            compiler = next((shutil.which(name) for name in ("c++", "g++", "clang++")
                             if shutil.which(name)), None)
            if compiler:
                command = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                           "-I", str(PROJECT / "src"), str(source), "-o", str(executable)]
            elif os.name == "nt":
                if shutil.which("cl"):
                    setup = ""
                else:
                    installer = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
                    vswhere = installer / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
                    if not vswhere.exists():
                        self.skipTest("No host C++ compiler installed")
                    location = subprocess.check_output(
                        [str(vswhere), "-latest", "-products", "*", "-requires",
                         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
                        text=True).strip()
                    if not location:
                        self.skipTest("No MSVC C++ tools installed")
                    setup = f'call "{location}/VC/Auxiliary/Build/vcvars64.bat" >nul\nif errorlevel 1 exit /b 1\n'
                batch = directory / "compile.cmd"
                batch.write_text(
                    "@echo off\n" + setup +
                    f'cl /nologo /std:c++17 /EHsc /W4 /WX /I"{PROJECT / "src"}" '
                    f'"{source}" /Fe:"{executable}"\n', encoding="utf-8")
                command = [os.environ.get("ComSpec", "cmd.exe"), "/d", "/c", str(batch)]
            else:
                self.skipTest("No host C++ compiler installed")
            build = subprocess.run(command, cwd=directory, capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
