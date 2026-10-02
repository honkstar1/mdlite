# Hosts mdlite.wlx64 in a WinForms window the way Lister does, times ListLoadW, and saves a screenshot.
param(
    [Parameter(Mandatory)] [string]$File,
    [string]$Dll = "$PSScriptRoot\..\build\mdlite.wlx64",
    [string]$Png = "$PSScriptRoot\..\build\smoke.png",
    [int]$Scroll = 0
)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type -TypeDefinition @"
using System; using System.Runtime.InteropServices;
public static class Wlx {
    [DllImport("kernel32", CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibraryW(string p);
    [DllImport("kernel32")] public static extern IntPtr GetProcAddress(IntPtr h, string n);
    [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet=CharSet.Unicode)]
    public delegate IntPtr ListLoadW(IntPtr parent, string file, int flags);
    [DllImport("user32")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
"@
$dll = (Resolve-Path $Dll).Path
$h = [Wlx]::LoadLibraryW($dll)
if ($h -eq [IntPtr]::Zero) { throw "LoadLibrary failed: $dll" }
$p = [Wlx]::GetProcAddress($h, "ListLoadW")
$fn = [Runtime.InteropServices.Marshal]::GetDelegateForFunctionPointer($p, [Wlx+ListLoadW])

$form = New-Object Windows.Forms.Form
$form.StartPosition = "Manual"; $form.Location = New-Object Drawing.Point(50, 50)
$form.ClientSize = New-Object Drawing.Size(1000, 800)
$form.TopMost = $true
$form.Show()
[Windows.Forms.Application]::DoEvents()

$sw = [Diagnostics.Stopwatch]::StartNew()
$child = $fn.Invoke($form.Handle, (Resolve-Path $File).Path, 0)
$sw.Stop()
"ListLoadW returned $child in $($sw.ElapsedMilliseconds) ms"
if ($child -eq [IntPtr]::Zero) { exit 1 }

for ($i = 0; $i -lt $Scroll; $i++) { [void][Wlx]::SendMessage($child, 0x20A, [IntPtr](-120 -shl 16 -band 0xFFFFFFFF), [IntPtr]0) }
$sw.Restart()
[Windows.Forms.Application]::DoEvents()
Start-Sleep -Milliseconds 300
[Windows.Forms.Application]::DoEvents()
"first paint pumped in $($sw.ElapsedMilliseconds) ms"

$r = $form.RectangleToScreen($form.ClientRectangle)
$bmp = New-Object Drawing.Bitmap($r.Width, $r.Height)
$g = [Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.Location, [Drawing.Point]::Empty, $r.Size)
$bmp.Save($Png, [Drawing.Imaging.ImageFormat]::Png)
$form.Close()
"saved $Png"
