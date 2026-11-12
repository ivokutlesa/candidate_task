namespace AutomationClient;

public enum DeviceFault
{
    OK,
    INTERLOCK,
    COMM_TIMEOUT
}

public sealed record DeviceStatus(
    long Sequence,
    double TemperatureC,
    bool FanOn,
    bool InterlockActive,
    DeviceFault Fault);
