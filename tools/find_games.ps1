# Lists installed SCS truck games, found through Steam's registry entry and library list.
# One line per game: <ATS|ETS2>|<game>\bin\win_x64
$roots = @()
foreach ($key in 'HKCU:\Software\Valve\Steam', 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam') {
    $item = Get-ItemProperty $key -ErrorAction SilentlyContinue
    if ($item.SteamPath) { $roots += $item.SteamPath }
    if ($item.InstallPath) { $roots += $item.InstallPath }
}
$roots += 'C:\Program Files (x86)\Steam'

$libraries = @()
foreach ($root in $roots) {
    $libraries += $root
    $vdf = Join-Path $root 'steamapps\libraryfolders.vdf'
    if (Test-Path $vdf) {
        foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
            $libraries += $m.Groups[1].Value -replace '\\\\', '\'
        }
    }
}

$games = @(
    @{ Id = 'ATS';  Folder = 'American Truck Simulator'; Exe = 'amtrucks.exe' },
    @{ Id = 'ETS2'; Folder = 'Euro Truck Simulator 2';   Exe = 'eurotrucks2.exe' }
)
foreach ($g in $games) {
    foreach ($lib in ($libraries | Select-Object -Unique)) {
        $dir = Join-Path $lib ('steamapps\common\' + $g.Folder + '\bin\win_x64')
        if (Test-Path (Join-Path $dir $g.Exe)) { $g.Id + '|' + (Resolve-Path $dir).Path; break }
    }
}
