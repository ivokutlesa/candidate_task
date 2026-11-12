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
        if (string.IsNullOrEmpty(line)) return false;

        string[] fields = line.Split(',');
        if (fields.Length != 6 || fields[0] != "STATUS") return false;

        if (!IsAsciiDigits(fields[1]) ||
            !long.TryParse(fields[1], NumberStyles.None, CultureInfo.InvariantCulture,
                out long sequence) ||
            sequence < 1)
            return false;

        if (!IsDecimal(fields[2]) ||
            !double.TryParse(fields[2], NumberStyles.AllowDecimalPoint,
                CultureInfo.InvariantCulture, out double temperature) ||
            !double.IsFinite(temperature) || temperature < 0 || temperature > 100)
            return false;

        if (fields[3] is not ("0" or "1") || fields[4] is not ("0" or "1"))
            return false;

        DeviceFault fault;
        switch (fields[5])
        {
            case "OK": fault = DeviceFault.OK; break;
            case "INTERLOCK": fault = DeviceFault.INTERLOCK; break;
            case "COMM_TIMEOUT": fault = DeviceFault.COMM_TIMEOUT; break;
            default: return false;
        }

        status = new DeviceStatus(
            sequence,
            temperature,
            fields[3] == "1",
            fields[4] == "1",
            fault);
        return true;
    }

    private static bool IsAsciiDigits(string value)
    {
        if (value.Length == 0) return false;
        foreach (char ch in value)
        {
            if (ch < '0' || ch > '9') return false;
        }
        return true;
    }

    private static bool IsDecimal(string value)
    {
        if (value.Length == 0) return false;

        bool decimalPointSeen = false;
        for (int i = 0; i < value.Length; i++)
        {
            char ch = value[i];
            if (ch >= '0' && ch <= '9') continue;
            if (ch != '.' || decimalPointSeen || i == 0 || i == value.Length - 1)
                return false;
            decimalPointSeen = true;
        }
        return true;
    }
}
