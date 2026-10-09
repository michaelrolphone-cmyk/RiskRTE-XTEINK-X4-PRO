"""Recorded X4 .29 DIO diagnostic selection; ordinary composition remains QIO."""
import hashlib
import io
import re
from pathlib import Path

DIO = 'dio-opi-80mhz'
DIO_BOOTLOADER_SHA256 = '1033730a6df733f53a7a347353c1c5450547f76e98e0746da633079310a563b9'
DIO_SDK_SHA256 = {
    'tools/sdk/esp32s3/bin/bootloader_dio_80m.elf': '787c681f6796924f7c9953d46ea45953001e1ad6a780c30e137bbdf8d21a1426',
    'tools/sdk/esp32s3/dio_opi/include/sdkconfig.h': 'f44a16b672d073fa7de730e2472d4d96feb251652440805a92d697b7a2e6b18b',
    'tools/sdk/esp32s3/dio_opi/libspi_flash.a': '30e4e8bacc903085e8c39250f3a1015edce6650f09c954e6ac45a238a151409d',
}
PRESERVED_SETTINGS = {
    'CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE': '1',
    'CONFIG_ESP32S3_BROWNOUT_DET': '1',
    'CONFIG_ESP32S3_BROWNOUT_DET_LVL': '7',
    'CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ': '240',
    'CONFIG_SPIRAM_BOOT_INIT': '1',
    'CONFIG_SPIRAM_MODE_OCT': '1',
    'CONFIG_SPIRAM_SPEED_80M': '1',
}


def require(value, message):
    if not value:
        raise ValueError(message)


def selected(record):
    if 'boot_flash_experiment' not in record:
        return False
    require(record['boot_flash_experiment'] == DIO, 'Invalid recorded boot flash experiment')
    return True


def compose_config(original, environment, record):
    selected(record)
    needle = b'pre:scripts/reproducible_build.py'
    require(needle in original, 'Pinned Runtime pre-build script entry missing')
    result = original.replace(needle, b'pre:x4-native/build.py')
    if selected(record):
        section = ('[env:' + environment + ']\n').encode()
        require(result.count(section) == 1, 'DIO composition requires one selected environment')
        body = result.split(section, 1)[1].split(b'\n[', 1)[0]
        require(b'board_build.arduino.memory_type' not in body and b'board_build.flash_mode' not in body,
                'DIO environment already overrides flash selection')
        result = result.replace(section, section + b'board_build.arduino.memory_type = dio_opi\nboard_build.flash_mode = dio\n', 1)
    return result


def verify_build(env, record):
    if not selected(record):
        return
    expected = {'build.arduino.memory_type': 'dio_opi', 'build.flash_mode': 'dio',
                'build.f_flash': '80000000L', 'build.f_cpu': '240000000L',
                'build.flash_size': '16MB', 'upload.flash_size': '16MB'}
    board = env.BoardConfig()
    for key, value in expected.items():
        require(str(board.get(key)) == value, 'DIO board selection differs from composition: ' + key)
    framework = Path(env.PioPlatform().get_package_dir('framework-arduinoespressif32'))
    for name, digest in DIO_SDK_SHA256.items():
        require(hashlib.sha256((framework / name).read_bytes()).hexdigest() == digest,
                'DIO SDK source differs: ' + name)
    config = (framework / 'tools/sdk/esp32s3/dio_opi/include/sdkconfig.h').read_text()
    for key, value in PRESERVED_SETTINGS.items():
        require(re.search(r'^#define ' + key + r' ' + value + r'\s*$', config, re.M),
                'DIO preserved SDK setting differs: ' + key)


def prove(blobs, record):
    """Recompute selected DIO proof from actual image headers and loaded ELF bytes."""
    if not selected(record):
        return None
    from elftools.elf.elffile import ELFFile
    loader = blobs['bootloader.bin']
    require(len(loader) == 14032 and hashlib.sha256(loader).hexdigest() == DIO_BOOTLOADER_SHA256,
            'Unreviewed DIO rollback bootloader binary')
    for name in ('bootloader.bin', 'firmware.bin'):
        blob = blobs[name]
        require(len(blob) >= 24 and blob[0] == 0xe9 and blob[2] == 2 and blob[3] == 0x4f,
                'DIO/80MHz/16MiB image header mismatch: ' + name)
    elf = ELFFile(io.BytesIO(blobs['firmware.elf']))
    require(elf.elfclass == 32 and elf.little_endian and elf['e_machine'] == 'EM_XTENSA',
            'DIO proof requires the actual Xtensa ELF')
    symbols = [s for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name == 'default_chip']
    require(len(symbols) == 1, 'DIO linked default_chip missing or ambiguous')
    symbol = symbols[0]
    require(isinstance(symbol['st_shndx'], int) and symbol['st_size'] == 32,
            'DIO linked default_chip layout mismatch')
    section = elf.get_section(symbol['st_shndx'])
    start = symbol['st_value'] - section['sh_addr']
    require(section['sh_type'] != 'SHT_NOBITS' and section['sh_flags'] & 2 and
            0 <= start and start + 32 <= section['sh_size'], 'DIO default_chip is not loaded data')
    mode = int.from_bytes(section.data()[start + 16:start + 20], 'little')
    require(mode == 3, 'DIO linked SPI flash read mode mismatch')
    return {'schema': 'x4.boot-flash-proof', 'schema_version': 1,
            'selection': DIO, 'composition_sha256': record['composition_sha256'],
            'bootloader_sha256': DIO_BOOTLOADER_SHA256,
            'firmware_sha256': hashlib.sha256(blobs['firmware.bin']).hexdigest(),
            'elf_sha256': hashlib.sha256(blobs['firmware.elf']).hexdigest(),
            'flash_mode': 'dio', 'memory_type': 'dio_opi', 'frequency_hz': 80000000,
            'flash_bytes': 16777216, 'sdk_pinned_files': DIO_SDK_SHA256,
            'preserved_sdk_settings': PRESERVED_SETTINGS,
            'linked_flash_mode': {'symbol': 'default_chip', 'symbol_bytes': 32,
                                  'read_mode_offset': 16, 'read_mode': mode, 'read_mode_name': 'SPI_FLASH_DIO'},
            'hardware_qualified': False}
