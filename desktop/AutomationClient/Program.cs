using AutomationClient;
using System.Globalization;

using var stop = new CancellationTokenSource();
Console.CancelKeyPress += (_, args) => { args.Cancel = true; stop.Cancel(); };

int port = 5042;
if (args.Length > 0 && !int.TryParse(args[0], out port))
{
    Console.Error.WriteLine("Port must be numeric.");
    return;
}

var client = new DeviceClient(port: port);
client.StatusReceived += status => Console.WriteLine(
    $"SEQ={status.Sequence} TEMP={status.TemperatureC:F1}C FAN={(status.FanOn ? "ON" : "OFF")} " +
    $"INTERLOCK={(status.InterlockActive ? "ACTIVE" : "CLEAR")} FAULT={status.Fault}");

Console.WriteLine("Commands: temp <0..100> | interlock on/off | heartbeat on/off | fragment | malformed | disconnect | quit");
Task deviceTask = client.RunAsync(stop.Token);
try
{
    while (!stop.IsCancellationRequested)
    {
        // Console reads ignore cancellation tokens, so wait on a separate task
        // to let Ctrl+C end the program without pressing Enter.
        string? input = await Task.Run(Console.In.ReadLine).WaitAsync(stop.Token);
        if (input is null) break;
        string cmd = input.Trim().ToLowerInvariant();
        if (cmd is "quit" or "exit") break;
        string? message = null;
        if (cmd.StartsWith("temp ") &&
            double.TryParse(cmd[5..], NumberStyles.Float, CultureInfo.InvariantCulture, out double temp) &&
            double.IsFinite(temp) && temp >= 0 && temp <= 100)
            message = "TEMP," + temp.ToString("0.0", CultureInfo.InvariantCulture);
        else if (cmd == "interlock on") message = "INTERLOCK,1";
        else if (cmd == "interlock off") message = "INTERLOCK,0";
        else if (cmd is "heartbeat on" or "heartbeat off")
        {
            // Simulates a frozen supervisor: the device should time out locally.
            client.HeartbeatEnabled = cmd == "heartbeat on";
            Console.WriteLine("Heartbeat " + (client.HeartbeatEnabled ? "resumed." : "paused."));
        }
        else if (cmd == "fragment") message = "INJECT,FRAGMENT";
        else if (cmd == "malformed") message = "INJECT,MALFORMED";
        else if (cmd == "disconnect") message = "INJECT,DISCONNECT";
        else Console.WriteLine("Unknown command.");

        if (message is not null && !await client.SendAsync(message, stop.Token))
            Console.WriteLine("Command not sent: device is disconnected.");
    }
}
catch (OperationCanceledException) when (stop.IsCancellationRequested) { }
finally
{
    stop.Cancel();
    try { await deviceTask; }
    catch (OperationCanceledException) { }
}
