# MMTTY (Qt)

A port of the MMTTY RTTY sound-card modem (originally Borland C++ Builder / VCL / Win32) to
**Qt 6 Widgets**, for Linux and Windows.

The signal-processing code (modulator, the four demodulators IIR / FIR / PLL / FFT, AFC, FFT, filter
design, Baudot tables) is the original algorithm code, ported with minimal changes. Everything that
was VCL or Win32 specific (UI, sound I/O, serial port, threads, ini files) is new Qt code.

## Layout

| Directory      | Content |
|----------------|---------|
| `src/dsp`      | Original DSP classes (`CFSKDEM`, `CFSKMOD`, `CRTTY`, `CFFT`, `CLMS`, FIR/IIR). No GUI or OS dependencies. `compat.h` supplies the Win32 type names and the global parameters the classes read. |
| `src/engine`   | `Engine`: real-time worker thread (replacement of `TSound`): sound in/out via Qt Multimedia, TX/RX switching, AFC, BPF / notch, WAV record and play. `PttPort` (RTS/DTR PTT and FSK line), `FskKeyer` (timed FSK edge thread), `Macro` (`%m`, `%c`, `%{CW}` ... expansion), `WavFile`. |
| `src/ui`       | `MainWindow`, scopes (spectrum, waterfall, XY, level bar), `SettingsDialog`, `AppSettings` (QSettings), `QsoLog` (ADIF). The window and dialog layouts are Qt Designer files, `MainWindow.ui` and `SettingsDialog.ui`; the scopes are promoted widgets (`Scopes.h`). Open the `.ui` files in Qt Designer to change the layout, widget names are the `ui->` members used in the code. |
| `src/remote`   | Engine remote mode API ("MMTTY Engine Remote Mode API", used by N1MM Logger+ and similar programs): `RemoteProtocol` (message numbers, shared memory layout, bit packing, command line), `RemoteApi` (protocol logic on top of `MainWindow` / `Engine`), `RemoteWin` (Windows transport: window messages, shared memory). |
| `tests`        | `dsp_loopback`, `macro_test`, `engine_test`, `fsk_test`, `gui_smoke`, `remote_test` (run with `ctest`). |

## Build

Requirements: CMake >= 3.16, a C++17 compiler, Qt 6 (Core, Gui, Widgets, **Multimedia**, **SerialPort**).

### Linux

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake qt6-base-dev qt6-multimedia-dev qt6-serialport-dev

cmake -S . -B build
cmake --build build -j
./build/mmtty
```

Serial-port PTT needs access to the device: `sudo usermod -aG dialout $USER` (log in again).

### Windows

Install Qt 6 (with the *Qt Multimedia* and *Qt Serial Port* add-ons) and CMake, then, in a Qt command prompt:

```bat
cmake -S . -B build -G "Ninja" -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\mingw_64
cmake --build build
windeployqt build\mmtty.exe
```

MSVC works as well (`-G "Visual Studio 17 2022"`).

### Tests

```sh
ctest --test-dir build --output-on-failure
```

`remote_test` is only built when `MMTTY_REMOTE_API` is on (see *Engine remote mode*).
The tests run without a sound card (`QT_QPA_PLATFORM=offscreen`, null audio):
a full TX -> internal loopback -> RX decode through all four demodulators, the AFC, WAV
record/play, macro expansion, a GUI run that drives the macro and TX state machine, and the remote API
(protocol code and `RemoteApi` logic through a fake host).

Command line: `mmtty --no-audio` (no sound device, internal loopback only), `mmtty --ini file.ini`.

### Engine remote mode

`mmtty` can act as the RTTY engine of another program (N1MM Logger+ and similar), using the original
MMTTY remote API (start options `-r -u -s -t -m ...`, window messages and shared memory). The transport
uses Win32 window messages and file mappings, so it is only active in the **Windows** executable.

The CMake option `MMTTY_REMOTE_API` controls the build: it is `ON` by default on Windows. On other systems it
is `OFF`; with `-DMMTTY_REMOTE_API=ON` only the portable part (protocol and `RemoteApi` logic) is built, for
`remote_test`, and the executable does not use it.

## Using it

* **Tuning**: click on the spectrum or waterfall to set the mark tone; right click places a notch.
  Mark / shift / baud are editable combo boxes. AFC follows the signal.
* **Transmit**: `TX` (Ctrl+T) keys the transmitter and sends what you type in the lower text box.
  Pressing `TX` again sends the rest of the text and returns to receive (`WAIT`). `TX OFF` (Esc) aborts.
* **Macros**: 16 buttons (F1-F12, Shift+F1-F4) and 4 input buttons (Alt+1-4). Defaults are those of the
  original English ini. Edit them in *File > Settings > Macros*.
  Control characters: `_` mark, `~` carrier off, `[` `]` diddle off/on, trailing `\` = return to receive
  when finished, trailing `#` = repeat, `%L` `%F` shifts, `%{TEXT}` CW ID.
  Variables: `%m %c %n %q %r %s %R %N %M %D %T %t`.
* **Log**: *Log QSO* appends to an ADIF file (`~/.local/share/DM1SV/dm1sv-mmtty/mmtty.adi` on Linux).
* **Sound files**: *File > Play / Record WAV*.
* **FSK on DTR/RTS**: wire the serial port's DTR (or RTS) through a level converter to the radio's FSK
  input. In *Settings > Sound & PTT* choose the port, set *FSK line* to DTR or RTS and tick *Invert* if
  your interface needs the opposite polarity (idle = mark). PTT can use the other line (DTR = FSK,
  RTS = PTT), or "None". While FSK is active no audio tones are sent. Each bit edge is scheduled
  from the modulator and fired by a dedicated keyer thread (sleep, then spin on the monotonic clock).
  SCHED_FIFO (Linux) / TIME_CRITICAL (Windows) priority is requested on a best-effort basis.
* Settings are stored with QSettings (`~/.config/mmtty-qt/mmtty.ini` on Linux, `%APPDATA%` on Windows).

## Not ported

These parts of the original were left out; the code that remains is structured so they can be added.

* The original's UART-TxD FSK mode (`txTXD`, `Comm.cpp`), where the UART hardware clocks the bits out at the
  RTTY baud rate. Software FSK on DTR/RTS (see *Using it*) is implemented instead. The external FSK plug-in (`CEXTFSK`) is also not ported.
* TNC emulation and the MMW / MML plug-in DLL interfaces. (The engine remote mode API with its `TXM_*` / `RXM_*` messages and shared memory is implemented, see *Engine remote mode*.)
* Radio CAT control, log-linking (`LogLink`), the original contest log, and the help files.
* The original `.ini` file is not read. Defaults were taken from `Mmtty English.ini`.
* Original forms are binary `.dfm` files, so the UI was redesigned rather than converted (as Qt Designer `.ui` files).
* Japanese (J-BELL) code set UI, extended-conversion dialogs, PSK/filter-graph dialogs.
* The sampling rate selection takes effect after a restart.

## Verification status

* Linux: builds without warnings; all five tests pass (`dsp_loopback`, `engine_test`, `fsk_test`, `macro_test`, `gui_smoke`), and `remote_test` as well with `-DMMTTY_REMOTE_API=ON`.
* FSK: verified only through a test hook that records line changes (keyer jitter below 10 µs, decoded
  keyed text correct, no framing errors). No real serial adapter or radio was used, and the
  real-time priority request was not tested. `FskKeyer.cpp` was compile-checked for Windows (MinGW).
* Remote mode: the protocol and `RemoteApi` logic are tested on Linux through a fake host. The Windows transport
  (`RemoteWin`: window messages, file mapping) is not covered by tests, and no real N1MM Logger+ was used.
* Windows: only the DSP core was checked (MinGW cross-compile, no warnings). The Qt / UI part was not
  built or run on Windows, and no test was done with real radio hardware or a real sound card here.
