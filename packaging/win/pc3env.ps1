# pc3env.ps1 - put this account's environment in order, or back.
#
#   powershell -ExecutionPolicy Bypass -File pc3env.ps1 -Add <folder>
#   powershell -ExecutionPolicy Bypass -File pc3env.ps1 -Remove <folder>
#
# install.cmd and uninstall.cmd call it; it is a file of its own because
# a PowerShell pipeline written inside a cmd script loses its pipes to
# cmd's parser, silently - the PATH was left untouched while everything
# else worked.
#
# Nothing here needs administrator rights: this account's own
# environment, and HKEY_CURRENT_USER for what a .bc file is.
param(
    [switch]$Add,
    [switch]$Remove,
    [Parameter(Mandatory = $true, Position = 0)][string]$Root
)

$ErrorActionPreference = 'Stop'
$bin = (Join-Path $Root 'bin')
$same = { param($a, $b) $a.TrimEnd('\') -ieq $b.TrimEnd('\') }

# ---- PATH ---------------------------------------------------------------
# Through .NET rather than setx, which truncates a PATH longer than 1024
# characters and leaves the account worse than it found it.
# Everything but our own entry is kept EXACTLY as found, empty entries
# included: plenty of accounts have a trailing semicolon, and an
# uninstall that quietly tidied it would have changed something it was
# not asked to change.
$p = [Environment]::GetEnvironmentVariable('Path', 'User')
if ($null -eq $p) { $p = '' }
$parts = @($p -split ';' | Where-Object { -not (& $same $_ $bin) })
if ($Add) {
    [Environment]::SetEnvironmentVariable('Path', (($parts + $bin) -join ';'), 'User')
    Write-Host "  PATH       $bin"
} else {
    [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')
    Write-Host "  PATH       $bin removed"
}

# ---- what a .bc file is --------------------------------------------------
# On the board and on Linux the object carries a #! line and runs by
# name.  Windows decides by extension, so .bc is registered here: then
# .\prog.bc runs it in a command prompt, ./prog.bc in PowerShell, and a
# double click in Explorer opens it.
#
# NOT PATHEXT.  Adding .BC there looks like the answer and is not: cmd
# uses PATHEXT to FIND a candidate and then CreateProcess it, which
# fails for anything that is not an executable image, and PowerShell
# will not run a bare name from the current directory at all.  Tried,
# measured, removed - the environment is the user's, and a setting that
# buys nothing does not belong in it.
if ($Add) {
    New-Item -Path 'HKCU:\Software\Classes\.bc' -Force | Out-Null
    Set-ItemProperty -Path 'HKCU:\Software\Classes\.bc' -Name '(default)' -Value 'PC3.Bytecode'
    New-Item -Path 'HKCU:\Software\Classes\PC3.Bytecode\shell\open\command' -Force | Out-Null
    Set-ItemProperty -Path 'HKCU:\Software\Classes\PC3.Bytecode' -Name '(default)' `
        -Value 'Pico Computer 3 program'
    Set-ItemProperty -Path 'HKCU:\Software\Classes\PC3.Bytecode\shell\open\command' -Name '(default)' `
        -Value ('"' + (Join-Path $bin 'bcrun.exe') + '" "%1" %*')
    Write-Host '  .bc        runs through bcrun'
} else {
    Remove-Item -Path 'HKCU:\Software\Classes\.bc' -Recurse -Force -ErrorAction SilentlyContinue
    Remove-Item -Path 'HKCU:\Software\Classes\PC3.Bytecode' -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host '  .bc        forgotten'
}

# ---- PATHEXT, only to undo it -------------------------------------------
# An early build of this script added .BC before the paragraph above was
# established.  Removing takes it out again; adding never puts it in.
if ($Remove) {
    $e = [Environment]::GetEnvironmentVariable('PATHEXT', 'User')
    if (-not [string]::IsNullOrEmpty($e)) {
        $ext = @($e -split ';' | Where-Object { $_ -ne '' -and $_.ToUpper() -ne '.BC' })
        $machine = @($env:PATHEXT -split ';' | Where-Object { $_ -ne '' })
        if (($ext -join ';') -ieq ($machine -join ';')) {
            # nothing of the account's own left in it
            [Environment]::SetEnvironmentVariable('PATHEXT', $null, 'User')
        } else {
            [Environment]::SetEnvironmentVariable('PATHEXT', ($ext -join ';'), 'User')
        }
        Write-Host '  PATHEXT    left as the machine has it'
    }
}
