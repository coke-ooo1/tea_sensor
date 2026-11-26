"""Validate firmware V1.0 bytes using the standard library and an isolated ACK peer."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_METRICS = {
    "soil_moisture": ("moisture_pct",),
    "soil_ph": ("ph",),
    "wind": ("speed_avg_m_s", "direction_avg_deg"),
    "rain": ("intensity_avg_mm_min",),
    "weather_box": ("temperature_c", "humidity_rh_pct", "light_lx", "pressure_kpa", "pm25_ug_m3", "pm10_ug_m3"),
}
EXPECTED_COUNTS = {"soil_moisture": 3, "soil_ph": 1, "wind": 15, "rain": 15, "weather_box": 3}


def reject_constant(value):
    raise ValueError(f"Invalid JSON constant {value}")


def unique_keys(pairs):
    values = dict(pairs)
    if len(values) != len(pairs):
        raise ValueError("Duplicate JSON field")
    return values


def read_json(frame):
    assert frame.endswith(b"\r\n") and frame.count(b"\r\n") == 1
    return json.loads(frame, parse_constant=reject_constant, object_pairs_hook=unique_keys)


def check_batch(message, complete):
    assert set(message) == {
        "protocol", "schema_version", "device_id", "seq", "batch_id", "upload_interval_s",
        "window_start_ts", "window_end_ts", "upload_ts", "time_valid", "batch_status",
        "window_start_uptime_ms", "window_end_uptime_ms", "uptime_ms", "data", "token",
    }
    assert message["protocol"] == "tea_sense.telemetry" and message["schema_version"] == "1.0"
    assert message["device_id"] == "tea_h743_001" and message["upload_interval_s"] == 900
    assert message["token"] == 'fixture"token\\test'
    assert re.fullmatch(r"tea_h743_001:[0-9a-f]{32}:1", message["batch_id"])
    assert message["seq"] == 1 and message["time_valid"] is False
    assert all(message[key] is None for key in ("window_start_ts", "window_end_ts", "upload_ts"))
    assert message["batch_status"] == ("COMPLETE" if complete else "PARTIAL")
    assert message["window_end_uptime_ms"] - message["window_start_uptime_ms"] == 900000
    assert message["window_end_uptime_ms"] <= message["uptime_ms"] <= message["window_end_uptime_ms"] + 6200
    data = message["data"]
    assert {group: len(rows) for group, rows in data.items()} == EXPECTED_COUNTS
    assert sum(len(EXPECTED_METRICS[group]) * len(data[group]) for group in data) == 67
    for group, rows in data.items():
        for i, row in enumerate(rows, 1):
            assert row["ts"] is None
            planned_ms = message["window_start_uptime_ms"]
            if group in ("wind", "rain"):
                assert row["uptime_ms"] == planned_ms + i * 60000 and row["sample_count"] == 12
            else:
                planned_ms += 900000 if group == "soil_ph" else i * 300000
                assert row["slot_uptime_ms"] == planned_ms <= row["uptime_ms"] <= planned_ms + 6000
            assert row["quality"] == ("OK" if complete else "INVALID"), row
            for metric in EXPECTED_METRICS[group]:
                assert (type(row[metric]) in (int, float) if complete else row[metric] is None), row
    if complete:
        assert all(row["sample_count"] == row["valid_count"] == 10 for row in data["soil_moisture"])
        assert all(row["moisture_pct"] == 50 for row in data["soil_moisture"])
        assert all(row["direction_quality"] == "OK" for row in data["wind"])
        assert all(row["intensity_avg_mm_min"] == row["intensity_max_mm_min"] == 4 for row in data["rain"])
        assert all(row["temperature_c"] == -5.5 and row["uptime_ms"] > row["slot_uptime_ms"]
                   for row in data["weather_box"])
    else:
        assert all(row["intensity_max_mm_min"] is None for row in data["rain"])


def run_ack_peer(listener):
    stored = {}
    for _ in range(2):
        connection, _ = listener.accept()
        with connection, connection.makefile("rb") as stream:
            connection.settimeout(3)
            frame = stream.readline(12288)
            request = read_json(frame)
            key = (request["device_id"], request["batch_id"])
            duplicate = key in stored
            assert not duplicate or stored[key] == frame
            stored[key] = frame
            ack = dict(protocol="tea_sense.ack", schema_version="1.0", device_id=request["device_id"],
                       seq=request["seq"], batch_id=request["batch_id"], status="duplicate" if duplicate else "accepted")
            connection.sendall(json.dumps(ack, separators=(",", ":")).encode("utf-8") + b"\r\n")
    assert len(stored) == 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sample-out", type=Path)
    args = parser.parse_args()
    gcc = os.environ.get("APP_TEST_GCC") or shutil.which("gcc")
    if not gcc:
        raise SystemExit("Set APP_TEST_GCC")
    env = dict(os.environ)
    env["PATH"] = str(Path(gcc).parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="tea_v1_wire_") as directory:
        temp = Path(directory)
        token = 'fixture"token\\test'
        (temp / "app_secrets.h").write_text(
            "#define APP_INGEST_TOKEN_CONFIGURED 1\n#define APP_INGEST_TOKEN_JSON " + json.dumps(json.dumps(token)) + "\n"
        )
        executable = temp / "wire.exe"
        includes = (temp, ROOT / "Tests/stubs", ROOT / "App/Inc", ROOT / "Sensors/Inc",
                    ROOT / "Bsp/Inc", ROOT / "Middlewares/Third_Party/coreJSON")
        sources = [source for layer in ("App", "Sensors", "Bsp") for source in (ROOT / layer / "Src").glob("*.c")]
        sources.extend(ROOT / path for path in ("Middlewares/Third_Party/coreJSON/core_json.c",
                                               "Tests/host_stdio.c", "Tests/dtu_host_test.c"))
        command = [gcc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fno-builtin", "-D__USE_MINGW_ANSI_STDIO=0",
                   "-include", str(ROOT / "Tests/stubs/stdio_fault.h")]
        subprocess.run(command + ["-I" + str(path) for path in includes] + list(map(str, sources)) +
                       ["-lm", "-o", str(executable)], env=env, check=True)
        first, retry, reboot = subprocess.check_output([str(executable), "--wire"], env=env).splitlines(keepends=True)
        partial = subprocess.check_output([str(executable), "--partial"], env=env).splitlines(keepends=True)[0]
        request = read_json(first)
        check_batch(request, complete=True)
        check_batch(read_json(partial), complete=False)
        assert first == retry and request["seq"] == read_json(reboot)["seq"]
        assert request["batch_id"] != read_json(reboot)["batch_id"]
        assert len(first) < 12000
        with socket.socket() as listener, ThreadPoolExecutor(max_workers=1) as worker:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(3)
            task = worker.submit(run_ack_peer, listener)
            for frame, status in ((first, "accepted"), (retry, "duplicate")):
                with socket.create_connection(listener.getsockname(), timeout=3) as connection:
                    for offset in range(0, len(frame), 61):
                        connection.sendall(frame[offset:offset + 61])
                    with connection.makefile("rb") as stream:
                        ack = stream.readline(512)
                assert read_json(ack)["status"] == status
                subprocess.run([str(executable), "--ack", request["batch_id"], str(request["seq"])],
                               input=ack, env=env, check=True)
            task.result()
        if args.sample_out:
            args.sample_out.write_bytes(first)
        print(f"PASS: {len(first)}-byte complete C frame, 67 primary values, 15 rain peaks, failed/null frame")
        print("PASS: byte-identical retry, new boot identity, fragmented loopback TCP, C accepts accepted/duplicate ACK")


if __name__ == "__main__":
    main()
