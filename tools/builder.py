"""BJ2GC Builder: the whole build in a window.

    Double-click "Build BJ2GC.bat" (or: python tools/builder.py)

It checks what the build needs (your Steam copy of the game, Pillow,
devkitPro, Octave-libogc) and says how to fix what's missing; then one
button makes the data from your copy (tools/make_data.py) and packages the
disc with Octave, and shows where the ISO is. The Octave-libogc folder it
remembers in tools/.builder.json (not committed).
"""
import json
import os
import re
import queue
import subprocess
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, ttk

HERE = Path(__file__).resolve().parents[1]
PROJECT = HERE / 'BJ2GC'
DATA = PROJECT / 'Scripts' / 'Data'
ISO = PROJECT / 'Packaged' / 'GameCube' / 'BJ2GC.iso'
SETTINGS = Path(__file__).with_name('.builder.json')
LOW_PRIORITY = 0x4000 | 0x08000000  # below normal, no console window (Windows)


def msys(path):
    """C:\\devkitPro -> /c/devkitPro, as devkitPro's makefiles want it."""
    p = Path(path).as_posix()
    return f'/{p[0].lower()}{p[2:]}' if len(p) > 1 and p[1] == ':' else p


def find_devkitpro():
    for candidate in (os.environ.get('DEVKITPRO_WIN'), os.environ.get('DEVKITPRO'), r'C:\devkitPro',
                      r'C:\gekko-toolchain'):
        if not candidate:
            continue
        if candidate.startswith('/') and len(candidate) > 2 and candidate[2] == '/':
            candidate = f'{candidate[1].upper()}:{candidate[2:]}'  # /c/devkitPro
        elif candidate.startswith('/opt/devkitpro'):
            candidate = r'C:\devkitPro'
        if (Path(candidate) / 'devkitPPC' / 'bin' / 'powerpc-eabi-gcc.exe').exists():
            return Path(candidate)
    return None


# --- the GameCube toolchain: devkitPro's install or gekko-toolchain, whichever is chosen ---------
TOOLCHAIN_SETTING = Path(__file__).with_name('.toolchain.json')   # (not in git)


def toolchain_path(candidate):
    """A toolchain folder from a folder or a DEVKITPRO-style value (/c/devkitPro), or None."""
    if not candidate:
        return None
    if candidate.startswith('/') and len(candidate) > 2 and candidate[2] == '/':
        candidate = f'{candidate[1].upper()}:{candidate[2:]}'
    elif candidate.startswith('/opt/devkitpro'):
        candidate = r'C:\devkitPro'
    path = Path(candidate)
    return path if (path / 'devkitPPC' / 'bin' / 'powerpc-eabi-gcc.exe').exists() else None


def toolchain_label(path):
    """"gekko-toolchain r49.2  (C:\\gekko-toolchain)" or "devkitPro r49.2  (C:\\devkitPro)"."""
    kind = 'gekko-toolchain' if (path / 'VERSIONS.txt').exists() and (path / 'Install.bat').exists() else 'devkitPro'
    version = ''
    try:
        version = next(line.split()[1].split('-')[0] for line in (path / 'VERSIONS.txt').read_text().splitlines()
                       if line.startswith('devkitPPC '))
    except (OSError, StopIteration, IndexError):
        # (Windows matches names regardless of case: devkitppc-rules would match a glob too)
        db = path / 'msys2' / 'var' / 'lib' / 'pacman' / 'local'
        found = sorted(m.group(1) for d in (db.iterdir() if db.is_dir() else ())
                       for m in [re.match(r'devkitPPC-(r[0-9.]+)-', d.name)] if m)
        if found:
            version = found[-1]
    return f'{kind} {version}'.strip() + f'  ({path})'


def toolchain_settings():
    try:
        return json.loads(TOOLCHAIN_SETTING.read_text())
    except (OSError, ValueError):
        return {}


def find_toolchains():
    """Every toolchain there is: DEVKITPRO's, C:\\devkitPro, C:\\gekko-toolchain, and folders chosen before."""
    found = []
    for candidate in (os.environ.get('DEVKITPRO_WIN'), os.environ.get('DEVKITPRO'), r'C:\devkitPro',
                      r'C:\gekko-toolchain', *toolchain_settings().get('folders', [])):
        path = toolchain_path(candidate)
        if path and all(path.resolve() != other.resolve() for other in found):
            found.append(path)
    return found


class Builder:
    def __init__(self, root):
        self.root = root
        self.lines = queue.Queue()
        self.busy = False
        root.title('BJ2GC Builder')
        root.minsize(640, 520)
        settings = {}
        try:
            settings = json.loads(SETTINGS.read_text())
        except (OSError, ValueError):
            pass
        self.octave = tk.StringVar(value=settings.get('octave', str(HERE.parent / 'octave-libogc')))
        self.test_keys = tk.BooleanVar(value=False)
        self.remake = tk.BooleanVar(value=False)

        pad = {'padx': 10, 'pady': 4}
        ttk.Label(root, text='Bejeweled 2 for the GameCube', font=('Segoe UI', 14, 'bold')).pack(anchor='w', **pad)

        checks = ttk.LabelFrame(root, text='What the build needs')
        checks.pack(fill='x', **pad)
        self.rows = {}
        for key, title in (('game', 'Bejeweled 2 Deluxe (Steam)'), ('pillow', 'Python: Pillow'),
                           ('devkitpro', 'GameCube toolchain'), ('octave', 'Octave-libogc')):
            row = ttk.Frame(checks)
            row.pack(fill='x', padx=6, pady=2)
            mark = ttk.Label(row, width=3, font=('Segoe UI', 11, 'bold'))
            mark.pack(side='left')
            ttk.Label(row, text=title, width=26).pack(side='left')
            note = ttk.Label(row, foreground='#555')
            note.pack(side='left', fill='x', expand=True)
            if key == 'octave':
                ttk.Button(row, text='Choose...', command=self.choose_octave).pack(side='right')
            if key == 'devkitpro':
                self.add_toolchain_switch(row)
            self.rows[key] = (mark, note)

        options = ttk.Frame(root)
        options.pack(fill='x', **pad)
        ttk.Checkbutton(options, text='Make the data again', variable=self.remake).pack(side='left')
        ttk.Checkbutton(options, text='Test buttons (Z level done, L no moves, R power gem)',
                        variable=self.test_keys).pack(side='left', padx=12)

        buttons = ttk.Frame(root)
        buttons.pack(fill='x', **pad)
        self.build_button = ttk.Button(buttons, text='Build BJ2GC', command=self.build)
        self.build_button.pack(side='left')
        self.open_button = ttk.Button(buttons, text='Open the ISO folder', command=self.open_folder)
        self.open_button.pack(side='left', padx=8)
        self.progress = ttk.Progressbar(buttons, mode='indeterminate', length=180)  # shown while building

        self.status = ttk.Label(root, text='')
        self.status.pack(anchor='w', **pad)

        frame = ttk.Frame(root)
        frame.pack(fill='both', expand=True, padx=10, pady=(0, 10))
        self.log = tk.Text(frame, height=14, wrap='none', font=('Consolas', 9), state='disabled')
        scroll = ttk.Scrollbar(frame, command=self.log.yview)
        self.log.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y')
        self.log.pack(side='left', fill='both', expand=True)

        self.check()
        self.update_open()
        root.after(100, self.pump)

    # --- what the build needs -------------------------------------------------

    # --- the toolchain switch ----------------------------------------------

    def add_toolchain_switch(self, row):
        """The devkitPro row's drop-down (every toolchain found) and Choose button (another folder)."""
        ttk.Button(row, text='Choose...', command=self.choose_toolchain).pack(side='right')
        self.toolchain_box = ttk.Combobox(row, state='readonly', width=44)
        self.toolchain_box.pack(side='right', padx=4)
        self.toolchain_box.bind('<<ComboboxSelected>>', lambda _e: self.pick_toolchain(self.toolchain_box.current()))
        self.toolchains = []

    def refresh_toolchains(self):
        """The toolchains found, into the drop-down; self.devkitpro the one chosen (else the first)."""
        self.toolchains = find_toolchains()
        self.toolchain_box['values'] = [toolchain_label(p) for p in self.toolchains]
        chosen = toolchain_settings().get('chosen')
        index = next((i for i, p in enumerate(self.toolchains) if chosen and p.resolve() == Path(chosen).resolve()), 0)
        self.devkitpro = self.toolchains[index] if self.toolchains else None
        if self.toolchains:
            self.toolchain_box.current(index)
        else:
            self.toolchain_box.set('')

    def save_toolchain(self, path, add=False):
        settings = toolchain_settings()
        settings['chosen'] = str(path)
        if add and str(path) not in settings.setdefault('folders', []):
            settings['folders'].append(str(path))
        try:
            TOOLCHAIN_SETTING.write_text(json.dumps(settings))
        except OSError:
            pass

    def pick_toolchain(self, index):
        if 0 <= index < len(self.toolchains):
            self.save_toolchain(self.toolchains[index])
            self.check()

    def choose_toolchain(self):
        folder = filedialog.askdirectory(title='A GameCube toolchain: devkitPro or gekko-toolchain (the folder with devkitPPC in it)')
        if not folder:
            return
        path = toolchain_path(folder)
        if path is None:
            self.set_row('devkitpro', False, f'no devkitPPC in {folder}')
            return
        self.save_toolchain(path, add=True)
        self.check()

    def set_row(self, key, ok, note):
        mark, label = self.rows[key]
        mark.configure(text='OK' if ok else 'X', foreground='#1a7f37' if ok else '#c62828')
        label.configure(text=note)

    def check(self):
        ok = True
        sys.path.insert(0, str(HERE / 'tools'))
        try:
            import PIL  # noqa: F401
            self.set_row('pillow', True, 'installed')
        except ImportError:
            self.set_row('pillow', False, 'run: py -m pip install pillow')
            ok = False
        try:
            import make_data
            self.game = make_data.steam_game()
            self.set_row('game', True, str(self.game))
        except SystemExit as e:
            self.set_row('game', False, str(e))
            ok = False
        except ImportError:
            self.set_row('game', False, 'needs Pillow first')
            ok = False
        self.refresh_toolchains()
        if self.devkitpro:
            self.set_row('devkitpro', True, '')
        else:
            self.set_row('devkitpro', False, 'install devkitPro with devkitPPC (devkitpro.org), or gekko-toolchain (github.com/myuu-151/gekko-toolchain)')
            ok = False
        octave = Path(self.octave.get())
        if not (octave / 'Octave.exe').exists():
            self.set_row('octave', False, f'no Octave.exe in {octave}')
            ok = False
        elif not (octave / 'External' / 'ffmpeg' / 'bin' / 'ffmpeg.exe').exists():
            self.set_row('octave', False, 'no External/ffmpeg in it (Octave-libogc comes with one)')
            ok = False
        elif not (octave / 'Engine' / 'Build' / 'GCN' / 'libEngine.a').exists():
            self.set_row('octave', False, 'its GameCube engine library is not built (Engine/Build/GCN/libEngine.a)')
            ok = False
        else:
            self.set_row('octave', True, str(octave))
        self.ready = ok
        self.build_button.configure(state='normal' if ok and not self.busy else 'disabled')
        self.status.configure(text='Ready to build.' if ok else 'Fix the X items above, then build.')
        return ok

    def choose_octave(self):
        folder = filedialog.askdirectory(title='The Octave-libogc folder', initialdir=self.octave.get())
        if folder:
            self.octave.set(folder)
            try:
                SETTINGS.write_text(json.dumps({'octave': folder}))
            except OSError:
                pass
            self.check()

    # --- building ---------------------------------------------------------

    def write(self, text):
        self.log.configure(state='normal')
        self.log.insert('end', text)
        self.log.see('end')
        self.log.configure(state='disabled')

    def pump(self):
        try:
            while True:
                item = self.lines.get_nowait()
                if isinstance(item, tuple):
                    self.finished(*item)
                else:
                    self.write(item)
        except queue.Empty:
            pass
        self.root.after(100, self.pump)

    def build(self):
        if self.busy or not self.check():
            return
        self.busy = True
        self.build_button.configure(state='disabled')
        self.progress.pack(side='right')
        self.progress.start(12)
        self.status.configure(foreground='')
        self.log.configure(state='normal')
        self.log.delete('1.0', 'end')
        self.log.configure(state='disabled')
        threading.Thread(target=self.run_build, daemon=True).start()

    def run(self, args, cwd, env=None):
        """Runs a step, its output to the log; True if it succeeded."""
        proc = subprocess.Popen(args, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                creationflags=LOW_PRIORITY if os.name == 'nt' else 0)
        for raw in proc.stdout:
            self.lines.put(raw.decode('utf-8', 'replace'))
        return proc.wait() == 0

    def run_build(self):
        ok = True
        if self.remake.get() or not DATA.exists() or not any(DATA.iterdir()):
            self.lines.put('== Making the data from your copy of the game\n')
            self.root.after(0, lambda: self.status.configure(text='Making the data (a minute or two)...'))
            env = dict(os.environ, OCTAVE=self.octave.get())
            ok = self.run([sys.executable, '-u', str(HERE / 'tools' / 'make_data.py')], HERE, env)
        if ok:
            self.lines.put('\n== Building the disc with Octave\n')
            self.root.after(0, lambda: self.status.configure(text='Building the disc (a few minutes)...'))
            octave = Path(self.octave.get())
            dkp = self.devkitpro
            env = dict(os.environ)
            env['PATH'] = os.pathsep.join([str(dkp / 'devkitPPC' / 'bin'), str(dkp / 'tools' / 'bin'),
                                           str(dkp / 'msys2' / 'usr' / 'bin'), env.get('PATH', '')])
            env['DEVKITPRO'] = msys(dkp)
            env['DEVKITPPC'] = msys(dkp / 'devkitPPC')
            env['OCTAVE'] = octave.as_posix()
            env['AUTOPLAY'] = ''
            env['EXTRA'] = '-DBJ2_DEBUGKEYS=1' if self.test_keys.get() else ''
            # The options are compile flags: the file that reads them, and the
            # program, made again.
            for stale in (PROJECT / 'Intermediate' / 'GCN' / 'Bj2App.o', PROJECT / 'Build' / 'GCN' / 'BJ2GC.dol',
                          ISO):
                try:
                    stale.unlink()
                except OSError:
                    pass
            self.run([str(octave / 'Octave.exe'), '-headless', '-project', (PROJECT / 'BJ2GC.octp').as_posix(),
                      '-build', 'GameCube'], octave, env)
            ok = ISO.exists()
        self.lines.put((ok,))

    def finished(self, ok):
        self.busy = False
        self.progress.stop()
        self.progress.pack_forget()
        self.build_button.configure(state='normal' if self.ready else 'disabled')
        if ok:
            size = ISO.stat().st_size / (1024 * 1024)
            self.status.configure(text=f'Done: {ISO} ({size:.0f} MB)', foreground='#1a7f37')
            self.write(f'\n== Done: {ISO}\n')
        else:
            self.status.configure(text='The build failed: the log above says why.', foreground='#c62828')
        self.update_open()

    def update_open(self):
        self.open_button.configure(state='normal' if ISO.exists() else 'disabled')

    def open_folder(self):
        if ISO.exists():
            subprocess.Popen(['explorer', '/select,', str(ISO)])


def main():
    root = tk.Tk()
    try:
        ttk.Style().theme_use('vista')
    except tk.TclError:
        pass
    Builder(root)
    root.mainloop()


if __name__ == '__main__':
    main()
