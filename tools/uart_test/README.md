# UART test tool

Install the dependency in an isolated environment:

```powershell
cd tools\uart_test
python -m venv .venv
.\.venv\Scripts\Activate.ps1
python -m pip install -r requirements.txt
```

Useful commands (replace `COM5` with the actual port):

```powershell
python uart_test.py --self-test
python uart_test.py --list-ports
python uart_test.py --port COM5 --mode smoke --seed 20260829
python uart_test.py --port COM5 --mode dma --seed 20260829
python uart_test.py --port COM5 --mode random --cases 10000 --seed 20260829
python uart_test.py --port COM5 --mode stress --stress-bytes 100000 --seed 20260829
python uart_test.py --port COM5 --baud 115200 --mode waveform
python reconnect_test.py --port COM5 --cycles 5 --seed 20260829
```

The JSONL log records the fixed seed, case number, injection type, transmitted
bytes, fragment sizes, expected responses, actual responses, and failures. Reuse
the same seed and command when reproducing a failure.

`waveform` emits one `0x55` byte followed by a separate 1000-byte `0x55` block.
The host elapsed time is informational; use the logic analyzer for the measured
bit and byte durations. The firmware baud rate must match `--baud`.
