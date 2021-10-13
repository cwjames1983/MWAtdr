from pathlib import Path
import shutil

import numpy

from helpers import run_application
from mwatdr_utils import read_output_signal, write_inv_polyphase_filter


# Assume we are in project root directory.
TEST_DATA_PATH = Path('./test/input_data/integration')


def test_zero_signal_identity_ipfb(run_script: Path, working_dir: Path) -> None:
    # Test of valid input data, with all zeros signal data and the identity inverse polyphase filter.
    # Expected output signal is all zeros.

    # Uses observation 1294797712, with signal data starting at 1294797712, because that's all I have access to.
    # Contains 24 consecutive frequency channels from 109 to 132, and 128 tiles with X and Y polarisations.
    # A few tiles are flagged as faulty.

    working_dir = working_dir / 'zero_signal_identity_ipfb'
    working_dir.mkdir(exist_ok=False, parents=True)

    inv_polyphase_filter_path = working_dir / 'inverse_polyphase_filter.bin'
    inv_polyphase_filter = numpy.zeros((13, 256), dtype=numpy.float32)
    inv_polyphase_filter[:, -1] = 1      # Identity
    write_inv_polyphase_filter(inv_polyphase_filter_path, inv_polyphase_filter)

    input_dir = working_dir / 'input_data'
    input_dir.mkdir(exist_ok=False, parents=True)
    shutil.copyfile(TEST_DATA_PATH / '1294797712.metafits', input_dir / '1294797712.metafits')
    input_file_metadata_template = \
        "HDR_SIZE 4096\nPOPULATED 1\nOBS_ID 1294797712\nSUBOBS_ID 1294797712\nMODE VOLTAGE_START\n" \
        "UTC_START 2021-01-16-02:01:34\nOBS_OFFSET 0\nNBIT 8\nNPOL 2\nNTIMESAMPLES 64000\nNINPUTS 256\n" \
        "NINPUTS_XGPU 256\nAPPLY_PATH_WEIGHTS 0\nAPPLY_PATH_DELAYS 0\nINT_TIME_MSEC 500\nFSCRUNCH_FACTOR 50\n" \
        "APPLY_VIS_WEIGHTS 0\nTRANSFER_SIZE 5275648000\nPROJ_ID G0034\nEXPOSURE_SECS 304\nCOARSE_CHANNEL {}\n" \
        "CORR_COARSE_CHANNEL {}\nSECS_PER_SUBOBS 8\nUNIXTIME 1610762494\nUNIXTIME_MSEC 0\nFINE_CHAN_WIDTH_HZ 10000\n" \
        "NFINE_CHAN 128\nBANDWIDTH_HZ 1280000\nSAMPLE_RATE 1280000\nMC_IP 0.0.0.0\nMC_PORT 0\nMC_SRC_IP 0.0.0.0\n"
    for i, channel in enumerate(range(109, 132 + 1)):
        metadata = input_file_metadata_template.format(channel, i + 1).encode('ascii')
        metadata_padding = bytes(4096 - len(metadata))
        zero_block = bytes(256 * 64000 * 2)
        file_path = input_dir / f'1294797712_1294797712_{channel}.sub'
        with open(file_path, 'wb') as file:
            file.write(metadata)
            file.write(metadata_padding)
            file.write(zero_block)
            for _ in range(160):
                file.write(zero_block)

    output_dir = working_dir / 'output_dir'
    output_dir.mkdir(exist_ok=False, parents=True)

    result = run_application(run_script, input_dir, '1294797712', '1294797712', inv_polyphase_filter_path, output_dir, 'false')
    
    assert result.returncode == 0

    # Note: these tiles are flagged as faulty and will be skipped: 102, 115, 151, 164, 999, 2013, 2017, 2044, 2047
    tiles = [
        51, 52, 53, 54, 55, 56, 57, 58, 71, 72, 73, 74, 75, 76, 77, 78, 101, 104, 105,
		106, 107, 108, 111, 112, 113, 114, 116, 117, 118, 121, 122, 123, 124, 125, 126,
		127, 128, 131, 132, 133, 134, 135, 136, 137, 138, 141, 142, 143, 144, 145, 146, 147,
		148, 152, 153, 154, 155, 156, 157, 158, 161, 162, 163, 165, 166, 167, 168,
		2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012, 2014,
		2015, 2016, 2018, 2019, 2020, 2021, 2022, 2023, 2024, 2025, 2026, 2027, 2028, 2029,
		2030, 2031, 2032, 2033, 2034, 2035, 2036, 2037, 2038, 2039, 2040, 2041, 2042, 2043,
		2045, 2046, 2048, 2049, 2050, 2051, 2052, 2053, 2054, 2055, 2056
    ]
    tile_output_filenames = []
    for tile in tiles:
        tile_output_filenames.append(f'1294797712_1294797712_{tile}_X.bin')
        tile_output_filenames.append(f'1294797712_1294797712_{tile}_Y.bin')

    log_file_name = '1294797712_1294797712_outputlog.txt'

    # Check there's output for all the tiles and polarisations, plus the log file.
    output_dir_contents = [entry.name for entry in output_dir.iterdir()]
    assert set(output_dir_contents) == set(tile_output_filenames + [log_file_name])

    # Note: need to manually inspect output log file.

    for filename in tile_output_filenames:
        signal = read_output_signal(output_dir / filename)
        # Check signal is all zeros. Note this checks the accuracy of the output signal to as high precision as is
        # possible, stricter than the SRS requirement.
        assert signal.min() == 0 and signal.max() == 0
        # Check that the output downsampling is as expected. The new sampling frequency is 54, manually calculated.
        assert len(signal) == 160 * 64000 * 54
        del signal      # Really don't want big array hanging around
