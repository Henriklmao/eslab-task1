import subprocess
import pytest
from pathlib import Path

BUILD_TESTS_DIR = Path(__file__).parent.parent / "build" / "tests"


def run_gtest(executable_name, test_name):
    result = subprocess.run(
        [BUILD_TESTS_DIR / executable_name, f"--gtest_filter={test_name}"],
        capture_output=True,
        text=True,
    )
    passed = "[  PASSED  ] 1 test." in result.stdout
    return passed, result.stdout, result.stderr


@pytest.mark.parametrize("test_case", [
    "ControllerTest.BasicTest",
    "ControllerTest.IntegralTest",
    "ControllerTest.ProportionalTest",
    "ControllerTest.ProportionalTest2",
    "ControllerTest.DerivativeTest",
    "ControllerTest.ResetTest",
])
def test_gtest_wrapper(test_case):
    passed, stdout, stderr = run_gtest("test_controller", test_case)
    assert passed, f"GTest '{test_case}' failed:\n{stdout}\n{stderr}"


@pytest.mark.parametrize("test_case", [
    "SimulatorDelayIndexTest.ZeroDelayUsesCurrentIndex",
    "SimulatorDelayIndexTest.DelayUsesPreviousBufferIndex",
    "SimulatorDelayIndexTest.JitterAddsToDelay",
    "SimulatorDelayIndexTest.WrapsAroundCircularBuffer",
    "SimulatorDelayIndexTest.ClampsDelayToOldestBufferedSample",
    "SimulatorDelayIndexTest.InvalidTimingFallsBackToZeroIndex",
])
def test_simulator_gtest_wrapper(test_case):
    passed, stdout, stderr = run_gtest("test_simulator", test_case)
    assert passed, f"GTest '{test_case}' failed:\n{stdout}\n{stderr}"
