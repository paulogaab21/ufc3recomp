# =============================================================================
#  Compilar.ps1 - gera "UFC3 Launcher.exe" a partir de src\.
#
#  O launcher e' um unico executavel, sem arquivos soltos ao lado: a interface
#  e as imagens vao embutidas como recursos. Isso e' proposital -- ninguem
#  consegue alterar a interface ou trocar as imagens depois de compilado.
#
#  Uso:  .\Compilar.ps1
# =============================================================================
$ErrorActionPreference = 'Stop'

$raiz = $PSScriptRoot
$src  = Join-Path $raiz 'src'
$net  = "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319"
$csc  = Join-Path $net 'csc.exe'
$wpf  = Join-Path $net 'WPF'
$exe  = Join-Path $raiz 'UFC3 Launcher.exe'

if (-not (Test-Path $csc)) { throw "csc.exe nao encontrado em $net" }

# recursos embutidos: nome-do-arquivo,nome-logico (o codigo pede pelo logico)
$recursos = @(
    'Interface.xaml', 'art.jpg',
    'FiraSans-Regular.ttf', 'FiraSansCondensed-Regular.ttf', 'FiraSansCondensed-Bold.ttf'
)
foreach ($r in $recursos) {
    if (-not (Test-Path (Join-Path $src $r))) { throw "recurso faltando: src\$r" }
}

# o exe fica travado enquanto o launcher estiver aberto
Get-Process 'UFC3 Launcher' -EA SilentlyContinue | ForEach-Object {
    Write-Host "  fechando launcher aberto (pid $($_.Id))" -ForegroundColor DarkGray
    $_.CloseMainWindow() | Out-Null
    if (-not $_.WaitForExit(3000)) { $_.Kill() }
}
Start-Sleep -Milliseconds 400

$args = @(
    '/nologo', '/target:winexe', '/platform:x64', '/optimize+'
    "/out:$exe"
    "/win32icon:$(Join-Path $src 'ufc3.ico')"
    "/reference:$wpf\PresentationCore.dll"
    "/reference:$wpf\PresentationFramework.dll"
    "/reference:$wpf\WindowsBase.dll"
    "/reference:$net\System.Xaml.dll"
)
$args += $recursos | ForEach-Object { "/resource:$_,$_" }
$args += 'Launcher.cs'

Push-Location $src
try {
    $saida = & $csc @args 2>&1
    $codigo = $LASTEXITCODE
} finally { Pop-Location }

$saida | ForEach-Object { Write-Host "  $_" }
if ($codigo -ne 0) { throw "csc falhou (codigo $codigo)" }

$kb = [math]::Round((Get-Item $exe).Length / 1KB, 1)
Write-Host "`n  pronto: $exe  ($kb KB)" -ForegroundColor Green
