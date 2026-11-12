using AutomationClient;
using System.Globalization;

int passed = 0;
int failed = 0;
void Test(string label, Func<bool> check)
{
    try
    {
        if (!check()) throw new Exception("unexpected parser result");
        Console.WriteLine("PASS " + label);
        passed++;
    }
    catch (Exception ex)
    {
        Console.WriteLine("FAIL " + label + ": " + ex.Message);
        failed++;
    }
}

Test("valid telemetry", () =>
    Protocol.TryParseStatus("STATUS,42,60.5,1,0,OK", out var s) &&
    s is { Sequence: 42, TemperatureC: 60.5, FanOn: true,
           InterlockActive: false, Fault: DeviceFault.OK });
Test("interlock telemetry", () =>
    Protocol.TryParseStatus("STATUS,43,70.0,0,1,INTERLOCK", out var s) &&
    s is { FanOn: false, InterlockActive: true, Fault: DeviceFault.INTERLOCK });
Test("timeout telemetry", () =>
    Protocol.TryParseStatus("STATUS,44,70.0,0,0,COMM_TIMEOUT", out var s) &&
    s is { FanOn: false, Fault: DeviceFault.COMM_TIMEOUT });
Test("reject extra fields", () =>
    !Protocol.TryParseStatus("STATUS,1,60.0,0,0,OK,EXTRA", out _));
Test("reject negative sequence", () =>
    !Protocol.TryParseStatus("STATUS,-1,60.0,0,0,OK", out _));
Test("reject invalid booleans", () =>
    !Protocol.TryParseStatus("STATUS,1,60.0,9,0,OK", out _) &&
    !Protocol.TryParseStatus("STATUS,1,60.0,0,2,OK", out _));
Test("reject NaN and infinity", () =>
    !Protocol.TryParseStatus("STATUS,1,NaN,0,0,OK", out _) &&
    !Protocol.TryParseStatus("STATUS,1,Infinity,0,0,OK", out _));
Test("reject temperature outside sensor range", () =>
    !Protocol.TryParseStatus("STATUS,1,-5.0,0,0,OK", out _) &&
    !Protocol.TryParseStatus("STATUS,1,120.0,0,0,OK", out _));
Test("reject unknown or numeric faults", () =>
    !Protocol.TryParseStatus("STATUS,1,60.0,0,0,UNKNOWN", out _) &&
    !Protocol.TryParseStatus("STATUS,1,60.0,0,0,999", out _));
Test("reject corrupt and incomplete messages", () =>
    !Protocol.TryParseStatus("STATUS,not-a-number,broken", out _) &&
    !Protocol.TryParseStatus("", out _) &&
    !Protocol.TryParseStatus(null, out _));
Test("use invariant decimal separator", () =>
{
    var before = CultureInfo.CurrentCulture;
    try
    {
        CultureInfo.CurrentCulture = CultureInfo.GetCultureInfo("fr-FR");
        return Protocol.TryParseStatus("STATUS,1,57.5,0,0,OK", out var s)
            && s?.TemperatureC == 57.5;
    }
    finally { CultureInfo.CurrentCulture = before; }
});

Console.WriteLine($"{passed}/{passed + failed} tests passed");
return failed == 0 ? 0 : 1;
