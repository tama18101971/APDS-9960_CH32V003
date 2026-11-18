using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.IO.Ports;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Forms;

internal sealed class GestureTerminal : Form
{
    private static readonly Regex StateLine = new Regex(
        @"^STATE v=1 addr=([0-9A-F]{8}) ack=(\d+) run=([01]) int=([01]) irq=(\d+) err=(\d+) raw=(\d+)$");
    private static readonly Regex GestureLine = new Regex(
        @"^GESTURE (LEFT|RIGHT|UP|DOWN) source=(POLL|EXTI|PIN)$");
    private static readonly Dictionary<string, string> Directions =
        new Dictionary<string, string> {
            { "UP", "← Влево" }, { "DOWN", "→ Вправо" },
            { "RIGHT", "↑ Вверх" }, { "LEFT", "↓ Вниз" }
        };

    private readonly SerialPort port;
    private readonly string wlink;
    private readonly string nm;
    private readonly Button startButton = new Button { Text = "Начать тест", AutoSize = true };
    private readonly Button stopButton = new Button { Text = "Остановить тест", AutoSize = true };
    private readonly CheckBox interruptBox = new CheckBox {
        Text = "Прерывания (INT → PC3)", AutoSize = true, Margin = new Padding(18, 8, 0, 0)
    };
    private readonly Label status = new Label { AutoSize = true, Padding = new Padding(3, 7, 0, 10) };
    private readonly RichTextBox journal = new RichTextBox {
        ReadOnly = true, Dock = DockStyle.Fill, BackColor = Color.FromArgb(248, 250, 252),
        ForeColor = Color.FromArgb(26, 40, 55), Font = new Font("Consolas", 11F),
        DetectUrls = false
    };
    private readonly Timer timer = new Timer { Interval = 30 };
    private readonly Stopwatch clock = Stopwatch.StartNew();

    private string address;
    private string input = "";
    private long lastState = -60000;
    private bool busy;
    private bool updatingBox;
    private bool connected;
    private bool running;
    private bool interrupt;
    private uint ack;
    private string edges = "0";
    private string error = "0";
    private string rawError = "0";
    private uint pendingTag;
    private TaskCompletionSource<bool> confirmation;
    private string operation = "Подключение";

    [STAThread]
    private static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Application.Run(new GestureTerminal(args.Length == 0 ? "COM12" : args[0]));
    }

    private GestureTerminal(string portName)
    {
        string core = Environment.GetEnvironmentVariable("PLATFORMIO_CORE_DIR");
        if (String.IsNullOrWhiteSpace(core))
        {
            core = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile),
                                ".platformio");
        }
        wlink = Path.Combine(core, "packages", "tool-wlink", "wlink.exe");
        nm = Path.Combine(core, "packages", "toolchain-riscv", "bin", "riscv-wch-elf-nm.exe");
        port = new SerialPort(portName, 115200);

        Text = "APDS-9960 — терминал жестов";
        Font = new Font("Segoe UI", 10F);
        ClientSize = new Size(736, 456);
        MinimumSize = new Size(568, 310);
        StartPosition = FormStartPosition.CenterScreen;
        var layout = new TableLayoutPanel {
            Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Padding = new Padding(16)
        };
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        layout.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        var toolbar = new FlowLayoutPanel { AutoSize = true, Dock = DockStyle.Fill, WrapContents = false };
        toolbar.Controls.AddRange(new Control[] { startButton, stopButton, interruptBox });
        layout.Controls.Add(toolbar, 0, 0);
        layout.Controls.Add(status, 0, 1);
        layout.Controls.Add(journal, 0, 2);
        Controls.Add(layout);

        Shown += async (sender, args) => await Connect();
        startButton.Click += async (sender, args) => await Apply(true, interrupt);
        stopButton.Click += async (sender, args) => await Apply(false, interrupt);
        interruptBox.CheckedChanged += async (sender, args) => {
            if (!updatingBox) { await Apply(running, interruptBox.Checked); }
        };
        timer.Tick += (sender, args) => Receive();
        FormClosing += async (sender, args) => {
            if (busy || (Present && running))
            {
                args.Cancel = true;
                if (!busy && await Apply(false, interrupt)) { Close(); }
            }
        };
        FormClosed += (sender, args) => {
            timer.Dispose();
            port.Dispose();
        };
        UpdateControls();
    }

    private bool Present { get { return connected && clock.ElapsedMilliseconds - lastState < 5000; } }

    private async Task Connect()
    {
        busy = true;
        UpdateControls();
        try
        {
            string elf = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "firmware.elf");
            string symbols = await RunTool(nm, String.Format(@"""{0}""", elf));
            Match symbol = Regex.Match(symbols,
                @"(?m)^([0-9A-Fa-f]{8}) [Bb] apds_terminal_command\r?$");
            uint location;
            if (!symbol.Success ||
                !UInt32.TryParse(symbol.Groups[1].Value, NumberStyles.HexNumber,
                                 CultureInfo.InvariantCulture, out location) ||
                location < 0x20000000u || location > 0x200007FCu || (location & 3u) != 0)
            {
                throw new InvalidOperationException("Не найден адрес команд в прошивке терминала.");
            }
            address = location.ToString("X8");
            string probes = await RunTool(wlink, "list");
            var devices = new HashSet<string>();
            foreach (Match match in Regex.Matches(probes, @"<WCH-Link#(\d+) "))
            {
                devices.Add(match.Groups[1].Value);
            }
            if (devices.Count != 1)
            {
                throw new InvalidOperationException("Для теста нужен один подключённый WCH-Link.");
            }
            port.Open();
            timer.Start();
            await EnableSdi();
            connected = true;
            Append("SDI подключён. Ожидаю ответ прошивки.");
        }
        catch (Exception ex)
        {
            timer.Stop();
            port.Close();
            operation = "Ошибка подключения";
            Append(ex.Message);
        }
        finally
        {
            busy = false;
            UpdateControls();
        }
    }

    private Task<string> EnableSdi()
    {
        return RunTool(wlink, "--chip CH32V003 sdi-print enable");
    }

    private async Task<bool> Apply(bool run, bool useInterrupt)
    {
        if (!Present || busy) { UpdateControls(); return false; }
        busy = true;
        operation = "Применяю команду";
        pendingTag = (ack % 65535) + 1;
        confirmation = new TaskCompletionSource<bool>();
        Task<bool> reply = confirmation.Task;
        uint command = 0xA5000000u | (pendingTag << 8) |
                       (run ? 1u : 0u) | (useInterrupt ? 2u : 0u);
        UpdateControls();
        try
        {
            string writeError = null;
            try
            {
                await RunTool(wlink, "--chip CH32V003 write-mem 0x" + address +
                              " 0x" + command.ToString("X8"));
            }
            catch (Exception ex)
            {
                writeError = ex.Message;
            }
            // Even a failed RAM access can take the probe out of SDI-print mode.
            await EnableSdi();
            if (writeError != null) { throw new InvalidOperationException(writeError); }
            if (await Task.WhenAny(reply, Task.Delay(6000)) != reply)
            {
                throw new TimeoutException("Прошивка не подтвердила команду.");
            }
            if (error != "0" || running != run || interrupt != useInterrupt)
            {
                throw new InvalidOperationException(String.Format(
                    "Команда не выполнена. Ошибка драйвера {0}, I²C {1}.", error, rawError));
            }
            string mode = useInterrupt ? "прерывания PC3" : "опрос";
            Append(run ? "Тест идёт · " + mode : "Тест остановлен · режим " + mode);
            return true;
        }
        catch (Exception ex)
        {
            Append(ex.Message);
            return false;
        }
        finally
        {
            confirmation = null;
            busy = false;
            UpdateControls();
        }
    }

    private void Receive()
    {
        try
        {
            input += port.ReadExisting();
        }
        catch (Exception ex)
        {
            timer.Stop();
            port.Close();
            connected = false;
            operation = "Связь потеряна";
            Append(ex.Message);
            UpdateControls();
            return;
        }
        int boundary = input.LastIndexOf('\n');
        if (boundary >= 0)
        {
            foreach (string line in input.Substring(0, boundary).Split('\n'))
            {
                HandleLine(line.TrimEnd('\r'));
            }
            input = input.Substring(boundary + 1);
        }
        if (input.Length > 4096) { input = ""; }
        UpdateControls();
    }

    private void HandleLine(string line)
    {
        Match state = StateLine.Match(line);
        uint tag;
        if (state.Success && state.Groups[1].Value == address &&
            UInt32.TryParse(state.Groups[2].Value, out tag) && tag <= 65535)
        {
            lastState = clock.ElapsedMilliseconds;
            ack = tag;
            running = state.Groups[3].Value == "1";
            interrupt = state.Groups[4].Value == "1";
            edges = state.Groups[5].Value;
            error = state.Groups[6].Value;
            rawError = state.Groups[7].Value;
            if (ack == pendingTag && confirmation != null) { confirmation.TrySetResult(true); }
            return;
        }
        if (!Present || !running || busy) { return; }
        Match gesture = GestureLine.Match(line);
        if (gesture.Success)
        {
            string name = gesture.Groups[1].Value;
            string source = gesture.Groups[2].Value;
            Append(String.Format("{0,-10}  GESTURE_{1,-5}  {2}", Directions[name], name, source));
        }
        else if (line.StartsWith("FAULT ", StringComparison.Ordinal))
        {
            Append("Ошибка датчика · " + line.Substring(6));
        }
    }

    private void UpdateControls()
    {
        bool available = Present && !busy;
        startButton.Enabled = available && !running;
        stopButton.Enabled = available && running;
        interruptBox.Enabled = available;
        if (!busy)
        {
            updatingBox = true;
            interruptBox.Checked = interrupt;
            updatingBox = false;
        }
        string state = busy || !connected ? operation :
            !Present ? "Нет ответа прошивки терминала" : running ? "Тест идёт" : "Тест остановлен";
        string detail = Present && !busy ?
            (interrupt ? " · INT PC3 · IRQ " + edges : " · опрос") : "";
        if (Present && !busy && error != "0")
        {
            detail += " · ошибка " + error + " / I²C " + rawError;
        }
        status.Text = port.PortName + "  ·  " + state + detail;
    }

    private void Append(string text)
    {
        if (journal.TextLength > 60000) { journal.Clear(); }
        journal.AppendText(DateTime.Now.ToString("HH:mm:ss.fff") + "  " + text + Environment.NewLine);
        journal.ScrollToCaret();
    }

    private static async Task<string> RunTool(string path, string args)
    {
        var start = new ProcessStartInfo(path, args) {
            UseShellExecute = false, CreateNoWindow = true,
            RedirectStandardOutput = true, RedirectStandardError = true
        };
        using (Process process = Process.Start(start))
        {
            Task<string> output = process.StandardOutput.ReadToEndAsync();
            Task<string> errors = process.StandardError.ReadToEndAsync();
            Task both = Task.WhenAll(output, errors);
            if (await Task.WhenAny(both, Task.Delay(5000)) != both || !process.WaitForExit(500))
            {
                if (!process.HasExited) { process.Kill(); }
                throw new TimeoutException(Path.GetFileName(path) + " не ответил.");
            }
            await both;
            if (process.ExitCode != 0)
            {
                throw new InvalidOperationException(Path.GetFileName(path) + ": " + errors.Result.Trim());
            }
            return output.Result + errors.Result;
        }
    }
}
