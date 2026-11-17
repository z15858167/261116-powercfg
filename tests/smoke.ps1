# smoke.ps1 - runtime smoke test for PowerToggle.exe
# Starts the app, verifies the process/window survive, then terminates it.
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot
$Exe  = Join-Path $Root "build\PowerToggle.exe"

if (-not (Test-Path $Exe)) { throw "PowerToggle.exe not found, run build.ps1 first" }

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PTWin {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int left, top, right, bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct NOTIFYICONIDENTIFIER {
        public int cbSize;
        public IntPtr hWnd;
        public uint uID;
        public Guid guidItem;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);
    [DllImport("shell32.dll")]
    public static extern int Shell_NotifyIconGetRect(ref NOTIFYICONIDENTIFIER id, out RECT rect);
}
"@

Get-Process -Name "PowerToggle*" -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$fail = 0
function Assert($cond, $name) {
    if ($cond) { Write-Host "[PASS] $name" }
    else { Write-Host "[FAIL] $name"; $script:fail++ }
}

# PE 子系统必须是 GUI(2)，否则会闪黑框
$peBytes = [System.IO.File]::ReadAllBytes($Exe)
$peOff   = [BitConverter]::ToInt32($peBytes, 0x3C)
$subsys  = [BitConverter]::ToUInt16($peBytes, $peOff + 24 + 68)
Write-Host "       PE subsystem: $subsys"
Assert ($subsys -eq 2)                          "exe: GUI subsystem (no console window)"

# 版本资源必须与 version.txt 一致，且每个版本都保留独立产物
$verTxt = ([System.IO.File]::ReadAllText((Join-Path $Root "version.txt"))).Trim()
Write-Host "       version.txt: $verTxt"
Assert ($verTxt -match '^\d+\.\d+\.\d+$')        "version: version.txt is x.y.z"
Assert (Test-Path (Join-Path $Root ("build\PowerToggle-" + $verTxt + ".exe"))) "version: versioned artifact kept"
$fv = (Get-Item $Exe).VersionInfo.FileVersion
Write-Host "       FileVersion: $fv"
Assert ($fv -eq $verTxt)                         "version: exe FileVersion matches version.txt"

# 资源里的中文必须编译成正确的 Unicode（源文件是 UTF-8，易被按系统代码页误读）
function Find-Utf16([byte[]]$bytes, [string]$text) {
    $pat = [Text.Encoding]::Unicode.GetBytes($text)
    for ($i = 0; $i -le $bytes.Length - $pat.Length; $i++) {
        $ok = $true
        for ($j = 0; $j -lt $pat.Length; $j++) {
            if ($bytes[$i + $j] -ne $pat[$j]) { $ok = $false; break }
        }
        if ($ok) { return $i }
    }
    return -1
}

$uPlan  = [string][char]0x7535 + [char]0x6E90 + [char]0x65B9 + [char]0x6848   # 电源方案
$uSave  = [string][char]0x4FDD + [char]0x5B58                                 # 保存
$uMojib = [string][char]0x9422 + [char]0x7535 + [char]0x7B2E                  # 典型乱码残留
Assert ((Find-Utf16 $peBytes $uPlan) -ge 0)     "res: dialog Chinese labels compiled as Unicode"
Assert ((Find-Utf16 $peBytes $uSave) -ge 0)     "res: button Chinese labels compiled as Unicode"
Assert ((Find-Utf16 $peBytes $uMojib) -lt 0)    "res: no mojibake string embedded"

# exe 必须自带应用图标（资源管理器 / 任务栏 / Alt-Tab 都读它）
try {
    Add-Type -AssemblyName System.Drawing
    $ico = [System.Drawing.Icon]::ExtractAssociatedIcon($Exe)
    Assert ($null -ne $ico)                     "res: exe embeds an application icon"
    if ($ico) { Write-Host ("       exe icon: {0}x{1}" -f $ico.Width, $ico.Height) }
} catch {
    Assert $false                               "res: exe embeds an application icon"
}

$proc = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 3

Assert ($null -ne $proc)                        "app: process started"
Assert (-not $proc.HasExited)                   "app: still running after 3s"
Assert ((Get-Process -Id $proc.Id -ErrorAction SilentlyContinue) -ne $null) "app: single instance alive"
Assert ((Get-Process -Name "PowerToggle*" -ErrorAction SilentlyContinue).Count -eq 1) "app: no extra PowerToggle* instances"

# 注意：PowerShell 会把 $null 转成空字符串，必须用 [NullString]::Value 才是真正的 NULL
$hwnd = [PTWin]::FindWindowW("PowerToggleHiddenWnd", [NullString]::Value)
Assert ($hwnd.ToInt64() -ne 0)                  "app: hidden message window created"

# 由 shell 直接确认该程序确实注册了托盘图标（uID = 1）
$trayId = New-Object PTWin+NOTIFYICONIDENTIFIER
$trayId.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($trayId)
$trayId.hWnd   = $hwnd
$trayId.uID    = 1
$trayRect = New-Object PTWin+RECT
$hr = [PTWin]::Shell_NotifyIconGetRect([ref]$trayId, [ref]$trayRect)
Assert ($hr -eq 0)                              "tray: shell confirms the app's tray icon exists"
if ($hr -eq 0) {
    Write-Host ("       tray icon at {0},{1} - {2},{3}" -f $trayRect.left, $trayRect.top, $trayRect.right, $trayRect.bottom)
    Assert (($trayRect.right - $trayRect.left) -gt 0) "tray: icon rectangle is non-empty"
}

Start-Sleep -Seconds 2
$proc.Refresh()
Assert (-not $proc.HasExited)                   "app: alive after 5s"
$ws = [math]::Round($proc.WorkingSet64 / 1MB, 1)
Write-Host "       working set: $ws MB"
Assert ($proc.WorkingSet64 -lt 40MB)            "app: working set under 40MB"

# 启动第二个实例应立刻退出（单实例）
$p2 = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 2
$p2.Refresh()
Assert ($p2.HasExited)                          "app: second instance exits immediately"

$count = @(Get-Process -Name "PowerToggle*" -ErrorAction SilentlyContinue).Count
Assert ($count -eq 1)                           "app: exactly one process"

# 无子进程（不应调用 powercfg.exe）
$children = @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($proc.Id)" -ErrorAction SilentlyContinue)
Assert ($children.Count -eq 0)                  "app: no child processes spawned"

Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800
$left = @(Get-Process -Name "PowerToggle*" -ErrorAction SilentlyContinue).Count
Assert ($left -eq 0)                            "app: terminated cleanly"

Write-Host ""
if ($fail -eq 0) { Write-Host "[smoke] ALL PASSED" } else { Write-Host "[smoke] FAILURES: $fail" }
exit $fail
