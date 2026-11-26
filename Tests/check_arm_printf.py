"""Optional CPU emulation of the real linked cloud-only application, with fake HAL.

Requires Python unicorn (or APP_ARM_EMULATOR_PATH pointing to its package dir)
and a fresh MDK build. No target board or debugger is used. Only main.o and formatter fixture credentials are replaced in a temporary image;
App/Sensors/Bsp code and Arm libc are production. No hardware timing is emulated.
"""
import os
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.stdout.reconfigure(encoding="utf-8")

if os.environ.get("APP_ARM_EMULATOR_PATH"):
    sys.path.insert(0, os.environ["APP_ARM_EMULATOR_PATH"])
try:
    from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_HOOK_CODE
    from unicorn.arm_const import UC_CPU_ARM_CORTEX_A15, UC_ARM_REG_C1_C0_2, UC_ARM_REG_FPEXC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_SP
except ImportError:
    raise SystemExit("Optional ARM printf execution requires Python unicorn / APP_ARM_EMULATOR_PATH")

ROOT = Path(__file__).resolve().parents[1]
MDK = ROOT / "MDK-ARM"
TOOLS = Path(os.environ.get("BSP_ARM_BIN", r"D:\Program Files\Keil_v5\ARM\ARMCC\bin"))
HARNESS = (ROOT / "Tests/arm_integration_main.c").read_text(encoding="utf-8")
def elf_sections_and_symbols(binary):
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", binary)
    sections = [struct.unpack_from("<IIIIIIIIII", binary, header[6] + index * header[11])
                for index in range(header[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        strings_section = sections[section[6]]
        strings = binary[strings_section[4]:strings_section[4] + strings_section[5]]
        for offset in range(section[4], section[4] + section[5], section[9]):
            name, address, size, info, other, index = struct.unpack_from("<IIIBBH", binary, offset)
            label = strings[name:strings.find(b"\0", name)].decode(errors="replace")
            if address:
                symbols[label] = address
    return sections, symbols

with tempfile.TemporaryDirectory(prefix="tea_sense_arm_printf_") as folder:
    folder = Path(folder)
    source, obj = folder / "test_main.c", folder / "test_main.o"
    source.write_text(HARNESS, encoding="utf-8")
    (folder / "app_secrets.h").write_text("#define APP_INGEST_TOKEN_CONFIGURED 1\n#define APP_INGEST_TOKEN_JSON "+json.dumps(json.dumps("fixture-arm"))+"\n")
    project = ET.parse(MDK / "tea_sense.uvprojx")
    compile_command = [str(TOOLS / "armcc.exe"), "--cpu", "Cortex-M7.fp.dp", "--c99", "--gnu", "-O1", "--split_sections"]
    compile_command += ["-D" + definition for definition in project.findtext(".//Cads/VariousControls/Define").split(",")]
    for include in project.findtext(".//Cads/VariousControls/IncludePath").split(";"):
        compile_command += ["-I", str((MDK / include).resolve())]
    compile_command += ["-I",str(folder), '-DAPP_DEVICE_ID="demo_h743_001"']
    # Put fixture credentials before App/Inc for both the harness and exact production formatter.
    compile_command[1:1]=["-I",str(folder)]
    subprocess.run(compile_command + ["-c", str(source), "-o", str(obj)], check=True)
    response = (MDK / "tea_sense/tea_sense.lnp").read_text()
    assert response.count('"tea_sense\\main.o"') == 1
    response = response.replace('"tea_sense\\main.o"', '"' + str(obj) + '"')
    for module in ("app_upload", "app_telemetry_json"):
        replacement = folder / f"{module}.o"
        subprocess.run(compile_command + ["-c", str(ROOT / f"App/Src/{module}.c"), "-o", str(replacement)], check=True)
        reference = f'"tea_sense\\{module}.o"'
        assert response.count(reference) == 1
        response = response.replace(reference, f'"{replacement}"')
    image = folder / "printf.axf"
    response = re.sub(r"--list[^\r\n]*", lambda _: f'--list "{folder / "printf.map"}" -o "{image}"', response)
    link = folder / "printf.lnp"
    link.write_text(response)
    subprocess.run([str(TOOLS / "armlink.exe"), "--via", str(link)], cwd=MDK, check=True)
    binary = image.read_bytes()
    sections, symbols = elf_sections_and_symbols(binary)
    # Unicorn's M-profile models cannot execute the H743 double-precision VFP.
    # Execute Thumb/VFP on its A15 model and simulate the PRIMASK instructions.
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    cpu.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_A15)
    cpu.reg_write(UC_ARM_REG_C1_C0_2, 0xF00000)
    cpu.reg_write(UC_ARM_REG_FPEXC, 1 << 30)
    for address, size in ((0x08000000, 0x200000), (0x20000000, 0x20000),
                          (0x24000000, 0x80000), (0x40000000, 0x10000), (0xE000E000, 0x2000)):
        cpu.mem_map(address, size)
    for section in sections:
        if section[2] & 2 and section[1] != 8 and section[5]:
            cpu.mem_write(section[3], binary[section[4]:section[4] + section[5]])
    cpu.mem_write(0xE000ED88, struct.pack("<I", 0xF00000))
    cpu.reg_write(UC_ARM_REG_SP, symbols["__initial_sp"])
    captured=bytearray()
    console_trace=bytearray()
    frames=[]
    checkpoints=[]
    minimum_sp=[symbols["__initial_sp"]]
    minimum_pc=[0]
    application_started=[False]
    calls=dict(adc=0,config=0,stop=0,calibration=0,write=0,malloc=0,dtu_tx=0,dtu_rx_starts=0,
               weather_tx=0,weather_rx_starts=0,rng=0,soil_reads=0,wind_reads=0,ad3_reads=0,ad4_reads=0,ad5_reads=0)
    def read_word(name):return struct.unpack("<I",cpu.mem_read(symbols[name],4))[0]
    def write_word(name,value):cpu.mem_write(symbols[name],struct.pack("<I",value))
    def return_value(value):
        cpu.reg_write(UC_ARM_REG_R0,value)
        cpu.reg_write(UC_ARM_REG_PC,cpu.reg_read(UC_ARM_REG_LR))
    def check_console(stage,output):
        assert output == b"", "Firmware emitted unexpected console data"
        assert read_word("g_test_printf_errors")==0
        assert read_word("g_test_acks") == stage
        assert read_word("g_test_rounds") == 180 * stage + 2
    def handle(name):
        if name=="HAL_GetTick":return_value(read_word("g_test_tick_ms"))
        elif name=="BspTime_ReadBootRandom":
            word = (0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00)[calls["rng"]]
            cpu.mem_write(cpu.reg_read(UC_ARM_REG_R0), struct.pack("<I", word))
            calls["rng"] += 1
            return_value(1)
        elif name=="BspTime_StopBootRandom":
            return_value(0)
        elif name=="BspTime_GetUniqueId":
            cpu.mem_write(cpu.reg_read(UC_ARM_REG_R0), struct.pack("<III", 0x01020304, 0x11121314, 0x21222324))
            return_value(0)
        elif name=="HAL_UART_GetState":
            cpu.mem_write(symbols["huart2"],struct.pack("<I",0x40004400))
            cpu.mem_write(0x4000441C,struct.pack("<I",1<<6))
            return_value(0x20)
        elif name=="HAL_UART_Transmit_IT":
            assert cpu.reg_read(UC_ARM_REG_R0)==symbols["huart6"] and calls["dtu_rx_starts"]>0
            length=cpu.reg_read(UC_ARM_REG_R2)
            payload=bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1),length))
            assert length==len(payload) and 8000<length<=12000 and payload.endswith(b"\r\n")
            message=json.loads(payload)
            sequence = calls["dtu_tx"] + 1
            assert message["seq"] == sequence and message["window_end_uptime_ms"] == sequence * 900000
            assert message["schema_version"] == "1.0" and message["device_id"] == "demo_h743_001"
            assert message["token"] == "fixture-arm" and message["time_valid"] is False
            assert message["batch_id"] == f"demo_h743_001:102030404474649cb88898e8ddeeff00:{sequence}"
            assert message["upload_interval_s"] == 900 and message["batch_status"] == "COMPLETE"
            assert 1800 <= message["uptime_ms"] - message["window_end_uptime_ms"] <= 6000
            data = message["data"]
            assert {key: len(values) for key, values in data.items()} == dict(
                soil_moisture=3, soil_ph=1, wind=15, rain=15, weather_box=3)
            for wind, rain in zip(data["wind"], data["rain"]):
                assert wind["speed_avg_m_s"] == 15 and wind["direction_avg_deg"] == 134.83
                assert wind["speed_valid_count"] == wind["direction_valid_count"] == rain["valid_count"] == 12
                assert rain["intensity_avg_mm_min"] == rain["intensity_max_mm_min"] == 4
                assert wind["uptime_ms"] == rain["uptime_ms"] and wind["ts"] is None
            for row in data["soil_moisture"]:
                assert row["moisture_pct"] == (60.85 if sequence == 1 else 0) and row["valid_count"] == 10
            assert data["soil_ph"][0]["ph"] == 6
            for row in data["weather_box"]:
                assert tuple(row[key] for key in ("temperature_c", "humidity_rh_pct", "light_lx", "pressure_kpa",
                                                  "pm25_ug_m3", "pm10_ug_m3")) == (19.4, 70.8, 1249, 103.73, 22, 23)
                assert row["quality"] == "OK" and row["uptime_ms"] > row["slot_uptime_ms"]
            ack = json.dumps(dict(protocol="tea_sense.ack", schema_version="1.0", device_id="demo_h743_001",
                                  seq=sequence, batch_id=message["batch_id"], status="accepted"),
                             separators=(",", ":")).encode() + b"\r\n"
            assert len(ack)<=512
            cpu.mem_write(symbols["g_test_ack"],ack);write_word("g_test_ack_length",len(ack));write_word("g_test_ack_ready",1)
            write_word("g_test_tx_pending",1)
            write_word("g_test_tx_due",read_word("g_test_tick_ms")+(length*10000+115199)//115200)
            del message["token"]
            frames.append(message)
            calls["dtu_tx"]+=1
            return_value(0)
        elif name=="HAL_UART_Transmit":
            uart=cpu.reg_read(UC_ARM_REG_R0)
            if uart==symbols["huart3"]:
                request=bytes(cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1),cpu.reg_read(UC_ARM_REG_R2)))
                register=request[3]
                golden={0:"010300000002C40B",2:"01030002000265CB",20:"010300140002840F",11:"0103000B0001F5C8"}
                assert request.hex().upper()==golden[register]
                words={0:(708,194),2:(0,1249),20:(22,23),11:(10373,)}[register]
                response=bytearray((1,3,len(words)*2))
                for word in words:response.extend(word.to_bytes(2,"big"))
                crc=65535
                for byte in response:
                    crc^=byte
                    for _ in range(8):crc=(crc>>1)^0xA001 if crc&1 else crc>>1
                response.extend(crc.to_bytes(2,"little"))
                cpu.mem_write(symbols["g_test_weather_reply"],bytes(response))
                cpu.mem_write(symbols["g_test_weather_length"],struct.pack("<I",len(response)))
                calls["weather_tx"]+=1;return_value(0);return
            raise AssertionError("Unexpected console UART transmission")
        elif name=="HAL_UART_Receive_IT":
            uart=cpu.reg_read(UC_ARM_REG_R0)
            assert cpu.reg_read(UC_ARM_REG_R2)==1
            if uart==symbols["huart3"]:
                cpu.mem_write(symbols["g_test_weather_rx"],struct.pack("<I",cpu.reg_read(UC_ARM_REG_R1)))
                calls["weather_rx_starts"]+=1
            else:
                assert uart==symbols["huart6"],"Gas route must remain disabled"
                write_word("g_test_dtu_rx_byte",cpu.reg_read(UC_ARM_REG_R1));calls["dtu_rx_starts"]+=1
            return_value(0)
        elif name in ("HAL_UART_AbortReceive","HAL_UART_AbortTransmit"):return_value(0)
        elif name=="HAL_ADCEx_Calibration_Start":calls["calibration"]+=1;return_value(0)
        elif name=="HAL_ADC_ConfigChannel":
            channel=struct.unpack("<I",cpu.mem_read(cpu.reg_read(UC_ARM_REG_R1),4))[0]
            inputs = ("soil", "wind", "ad3", "ad4", "ad5")
            assert channel in {read_word("g_test_" + label + "_channel") for label in inputs}
            calls["selected"]=channel;calls["config"]+=1;return_value(0)
        elif name=="HAL_ADC_Start":calls["adc"]+=1;return_value(0)
        elif name=="HAL_ADC_PollForConversion":
            return_value(0)
        elif name=="HAL_ADC_GetValue":
            for label,channel in (("soil","g_test_soil_channel"),("wind","g_test_wind_channel"),("ad3","g_test_ad3_channel"),("ad4","g_test_ad4_channel"),("ad5","g_test_ad5_channel")):
                if calls["selected"]==read_word(channel):
                    calls[label+"_reads"]+=1
                    return_value(read_word("g_test_raw" if label=="soil" else "g_test_"+label+"_raw"))
                    return
            raise AssertionError("Unknown ADC input")
        elif name=="HAL_ADC_Stop":
            calls["stop"]+=1
            return_value(0)
        elif name=="_sys_write":calls["write"]+=1
        elif name=="malloc":calls["malloc"]+=1
        elif name=="TestArm_Observe":
            stage=cpu.reg_read(UC_ARM_REG_R0);check_console(stage,bytes(captured));checkpoints.append(stage)
            console_trace.extend(captured);captured.clear()
        elif name=="TestArm_Done":cpu.emu_stop()
        elif name in ("Error_Handler","_sys_exit"):raise AssertionError("Unexpected ARM failure: "+name)
    names=["BspTime_ReadBootRandom","BspTime_StopBootRandom","BspTime_GetUniqueId","HAL_GetTick","HAL_UART_GetState","HAL_UART_Transmit","HAL_UART_Transmit_IT","HAL_UART_Receive_IT","HAL_UART_AbortReceive","HAL_UART_AbortTransmit",
           "HAL_ADCEx_Calibration_Start","HAL_ADC_ConfigChannel","HAL_ADC_Start","HAL_ADC_PollForConversion","HAL_ADC_GetValue","HAL_ADC_Stop",
           "_sys_write","malloc","TestArm_Observe","TestArm_Done","Error_Handler","_sys_exit"]
    addresses={symbols[name]&~1:name for name in names if name in symbols}
    primask = 0
    mask_ops = {}
    for section in sections:
        if section[2] & 4:
            code = binary[section[4]:section[4] + section[5]]
            for offset in range(0, len(code) - 3, 2):
                first, second = struct.unpack_from("<HH", code, offset)
                address = section[3] + offset
                if first == 0xF3EF and second & 0xF0FF == 0x8010:
                    mask_ops[address] = ("read", UC_ARM_REG_R0 + ((second >> 8) & 15), 4)
                elif first & 0xFFF0 == 0xF380 and second == 0x8810:
                    mask_ops[address] = ("write", UC_ARM_REG_R0 + (first & 15), 4)
                elif first == 0xB672:
                    mask_ops[address] = ("disable", None, 2)

    def hook(instance,address,size,user):
        global primask
        if address in mask_ops:
            operation, register, width = mask_ops[address]
            if operation == "read":
                instance.reg_write(register, primask)
            else:
                primask = 1 if operation == "disable" else instance.reg_read(register) & 1
            instance.reg_write(UC_ARM_REG_PC, (address + width) | 1)
            return
        sp=cpu.reg_read(UC_ARM_REG_SP)
        if address == (symbols["main"] & ~1):
            application_started[0]=True
        if application_started[0] and 0x20000000<=sp<minimum_sp[0]:
            minimum_sp[0]=sp;minimum_pc[0]=address
        if address in addresses:handle(addresses[address])
    cpu.hook_add(UC_HOOK_CODE,hook)
    try:
        cpu.emu_start(symbols["__main"] | 1, 0, count=1000000000)
    except UcError:
        pc = cpu.reg_read(UC_ARM_REG_PC)
        nearest = max((addr & ~1, name) for name, addr in symbols.items() if (addr & ~1) <= pc)
        print(hex(pc), nearest, bytes(cpu.mem_read(pc, 8)).hex(), calls, read_word("g_test_tick_ms"))
        raise
    assert checkpoints == [1, 2], (checkpoints, calls)
    assert calls["calibration"] == 1 and calls["rng"] == 4
    assert calls["dtu_tx"] == 2 and calls["weather_tx"] == 24 and calls["malloc"] == 0 and calls["write"] == 0, calls
    assert calls["ad5_reads"] == calls["wind_reads"] == calls["ad3_reads"] == 362, calls
    assert calls["soil_reads"] == 60 and calls["ad4_reads"] == 2, calls
    assert calls["config"] == 1148 and calls["stop"] == 2296, calls
    evidence=Path(os.environ.get("APP_ARM_EVIDENCE_DIR", str(ROOT/"Docs/validation")))
    evidence.mkdir(parents=True,exist_ok=True)
    prefix=os.environ.get("APP_ARM_EVIDENCE_PREFIX","integration")
    (evidence/(prefix+"_arm_console.txt")).write_bytes(console_trace)
    (evidence/(prefix+"_arm_frames_redacted.json")).write_text(json.dumps(frames,ensure_ascii=False,indent=2),encoding="utf-8")
    print(console_trace.decode("utf-8"),end="")
    print("PASS: real ARM/C library, two V1.0 fifteen-minute batches and ACKs, grouped reads, calibrated values, no console/malloc")
    nearest=sorted(((address&~1,name) for name,address in symbols.items() if (address&~1)<=minimum_pc[0]),reverse=True)[:1]
    assert symbols["__initial_sp"]-minimum_sp[0] <= 800, "Application stack must leave >=224 bytes in configured 1KiB stack" 
    print("Observed application CPU stack bytes:",symbols["__initial_sp"]-minimum_sp[0],"PC",hex(minimum_pc[0]),nearest,"initial_sp",hex(symbols["__initial_sp"]))
