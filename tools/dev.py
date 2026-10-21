#!/usr/bin/env python3
"""Build the native app and synchronize explicitly selected changes with upstream."""
import argparse
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
import shlex
import zipfile
import subprocess
import sys
from package_assets import WriteLicenses


ROOT = Path(__file__).resolve().parents[1]


def run(*args, capture=False, summary=None, step=None):
  if not capture:
    print("+ " + shlex.join(args), flush=True)
  try:
    result = subprocess.run(args, cwd=ROOT, check=True, text=True,
        stdout=subprocess.PIPE if capture else sys.stdout, stderr=sys.stderr)
  except (OSError, subprocess.CalledProcessError) as error:
    if summary is not None and step:
      reason = f"exit {error.returncode}" if isinstance(error, subprocess.CalledProcessError) else str(error)
      print(f"{step}: FAILED ({reason})", file=summary, flush=True)
    raise
  if summary is not None and step:
    print(f"{step}: OK", file=summary, flush=True)
  return result.stdout.strip() if capture else None


def require_clean():
  # Untracked files are never staged implicitly; Git protects them during rebase.
  if run("git", "status", "--porcelain", "--untracked-files=no", capture=True):
    raise RuntimeError("Tracked changes remain. Commit or set them aside before synchronizing.")


def upstream():
  branch = run("git", "symbolic-ref", "--short", "HEAD", capture=True)
  remote = run("git", "config", "--get", f"branch.{branch}.remote", capture=True)
  target = run("git", "config", "--get", f"branch.{branch}.merge", capture=True)
  if remote == ".":
    raise RuntimeError("The current branch must track a remote branch.")
  run("git", "rev-parse", "--verify", "@{upstream}", capture=True)
  return remote, target


def sync(remote, summary=None):
  require_clean()
  run("git", "fetch", remote, summary=summary, step="Fetch")
  run("git", "rebase", "@{upstream}", summary=summary, step="Rebase")


def build(summary=None):
  directory = ROOT / "build" / ("mac" if sys.platform == "darwin" else "native")
  configure = ["cmake", "-S", str(ROOT), "-B", str(directory), f"-DPython3_EXECUTABLE={sys.executable}"]
  if sys.platform == "darwin":
    configure += ["-G", "Xcode"]
  run(*configure, summary=summary, step="Configure")
  run("cmake", "--build", str(directory), "--config", "Release", "--target", "sc2k", "-j", "6",
      summary=summary, step="Build")
  binary = directory / "Release/sc2k.app/Contents/MacOS/sc2k" if sys.platform == "darwin" else directory / "sc2k"
  run(sys.executable, "tools/stamp_revision.py", str(binary), summary=summary, step="Git revision")


def build_windows(args, summary):
  directory = ROOT / "build/windows-x64"
  sdl, zlib = args.sdl.resolve(), args.zlib.resolve()
  run("cmake", "-S", str(ROOT), "-B", str(directory), "-G", "Ninja",
      f"-DCMAKE_TOOLCHAIN_FILE={ROOT / 'tools/windows-x64.cmake'}", "-DCMAKE_BUILD_TYPE=Release",
      f"-DPython3_EXECUTABLE={sys.executable}", f"-DCMAKE_FIND_ROOT_PATH={sdl};{zlib}",
      "-DZLIB_USE_STATIC_LIBS=ON", summary=summary, step="Configure Windows x64")
  run("cmake", "--build", str(directory), "--target", "sc2k", "-j", "6",
      summary=summary, step="Build Windows x64")
  run(sys.executable, "tools/stamp_revision.py", str(directory / "OpenSC2k.exe"),
      summary=summary, step="Git revision")
  (directory / "sc2k.exe").unlink(missing_ok=True)
  licenses = [('SDL2/LICENSE.txt', sdl.parent / 'LICENSE.txt'), ('zlib/LICENSE', args.zlib_license.resolve())]
  licenses += [(f'docs/licenses/{name}.txt', ROOT / f'docs/licenses/{name}.txt')
      for name in ('mingw-w64', 'mingw-winpthreads', 'gcc-runtime-exception')]
  WriteLicenses(ROOT, directory, directory / 'generated-assets/audio/sound-effects.json', licenses)
  # Include only runtime files and the combined attribution, never SDKs or build objects.
  files = [directory / name for name in ("OpenSC2k.exe", "SDL2.dll", "intro.sc2assets",
      "graphics.sc2assets", "audio.sc2assets", "licenses.txt")]
  archive = ROOT / "build/OpenSC2K-windows-x64.zip"
  with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as output:
    for path in files:
      output.write(path, Path("OpenSC2K") / path.relative_to(directory))
  if summary is not None: print(f"Windows package: {archive}", file=summary, flush=True)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  output = argparse.ArgumentParser(add_help=False)
  output.add_argument("--log", type=Path, help="Save full output; show operation results and the current commit")
  commands = parser.add_subparsers(dest="command", required=True)
  commands.add_parser("build", parents=[output],
      help="Configure and compile the normal native app; does not run tests")
  windows = commands.add_parser("build-windows", parents=[output], help="Cross-compile and package Windows x64")
  windows.add_argument("--sdl", type=Path, required=True, help="SDL2 x86_64-w64-mingw32 SDK directory")
  windows.add_argument("--zlib", type=Path, required=True, help="Windows x64 static zlib installation prefix")
  windows.add_argument("--zlib-license", type=Path, required=True, help="zlib source LICENSE file")
  commands.add_parser("status", parents=[output], help="Show the branch, worktree and recent commits")
  commands.add_parser("sync", parents=[output], help="Fetch and rebase a clean tracked worktree onto its upstream")
  commands.add_parser("sync-build", parents=[output], help="Fetch, rebase and build the normal native app")
  publish = commands.add_parser("publish", parents=[output], help="Stage, commit, fetch, rebase, build and push")
  publish.add_argument("-m", "--message", required=True, help="Commit message")
  publish.add_argument("paths", nargs="*", help="Files to stage; omit to commit the existing index")
  args = parser.parse_args()
  summary = sys.stdout
  try:
    if args.log is None:
      execute(args, summary)
    else:
      print(f"Full output: {args.log.resolve()}", flush=True)
      with args.log.open("w", encoding="utf-8", buffering=1) as log, redirect_stdout(log), redirect_stderr(log):
        try:
          execute(args, summary)
        except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
          print(f"dev: {error}", file=log)
          raise
  finally:
    commit = subprocess.run(["git", "log", "-1", "--oneline"], cwd=ROOT, text=True, capture_output=True)
    if commit.returncode == 0: print("HEAD: " + commit.stdout.strip(), file=summary, flush=True)


def execute(args, summary):
  if args.command == "build":
    build(summary)
  elif args.command == "build-windows":
    build_windows(args, summary)
  elif args.command == "status":
    run("git", "status", "--short", "--branch")
    run("git", "log", "-5", "--oneline")
  else:
    remote, target = upstream()
    if args.command == "publish":
      if args.paths:
        staged = run("git", "diff", "--cached", "--name-only", capture=True)
        if staged:
          raise RuntimeError("The index already contains changes. Commit them or unstage them before selecting files.")
        run("git", "add", "--", *args.paths, summary=summary, step="Stage files")
      if run("git", "diff", "--name-only", capture=True):
        raise RuntimeError("Unstaged tracked changes remain; include them explicitly or set them aside.")
      run("git", "diff", "--cached", "--check", summary=summary, step="Check staged changes")
      run("git", "commit", "-m", args.message, summary=summary, step="Commit")
    sync(remote, summary)
    if args.command in ("sync-build", "publish"):
      build(summary)
    if args.command == "publish":
      require_clean()
      run("git", "push", remote, f"HEAD:{target}", summary=summary, step="Push")


if __name__ == "__main__":
  try:
    main()
  except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
    print(f"dev: {error}", file=sys.stderr)
    sys.exit(1)
