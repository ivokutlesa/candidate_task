using System.Globalization;

namespace AutomationClient;

public static class Protocol
{
    // TODO (candidate): This deliberately permissive parser needs work.
    // The device sends STATUS,<seq>,<temp_c>,<fan_0_or_1>,<interlock_0_or_1>,<fault>.
    // Reject wrong field count, non-finite/out-of-range temperature,
    // negative sequence, invalid 0/1 flags, unknown fault values and values
    // encoded using the current machine's locale. The exact accepted format of
    // each field is defined in CANDIDATE_TASK.md. Never throw on bad input.
    // A telemetry error must not stop subsequent valid messages being read.
    public static bool TryParseStatus(string? line, out DeviceStatus? status)
    {
        status = null;
        if (line is null || !line.StartsWith("STATUS,")) return false;
        string[] fields = line.Split(',');
        if (fields.Length < 6) return false;
        if (!long.TryParse(fields[1], out long sequence)) return false;
        if (!double.TryParse(fields[2], out double temperature)) return false;
        if (!int.TryParse(fields[3], out int fan)) return false;
        if (!int.TryParse(fields[4], out int interlock)) return false;
        if (!Enum.TryParse(fields[5], out DeviceFault fault)) return false;
        status = new DeviceStatus(sequence, temperature, fan != 0, interlock != 0, fault);
        return true;
    }
}
