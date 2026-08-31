# =============================================================================
#  Empacotar.ps1 - monta a pasta que vai para quem so quer jogar.
#
#  O pacote leva o jogo ja compilado. Quem receber nao instala compilador,
#  cmake, ninja nem nada: abre o launcher, aponta a ISO do proprio disco, e
#  joga. O launcher percebe o modo pelo simples fato de existir um ufc3.exe
#  ao lado dele.
#
#  O que NAO vai junto, e nunca deve ir: qualquer arquivo do disco do jogo.
#  As texturas, audio e video continuam vindo do disco de quem joga.
#
#  Uso:  .\Empacotar.ps1  [-Destino <pasta>]
# =============================================================================
param(
    [string]$Destino = 'C:\Users\Gabriel\projects\UFC3Recomp-dist'
)
$ErrorActionPreference = 'Stop'

$raiz    = $PSScriptRoot
$build   = 'C:\Users\Gabriel\projects\UFC3Recomp\out\build\win-amd64-relwithdebinfo'
$retools = 'C:\Users\Gabriel\projects\toolchain\re-tools'
$redist  = 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC'

# O launcher tem que estar compilado e atual.
& (Join-Path $raiz 'Compilar.ps1')

New-Item -ItemType Directory -Force $Destino | Out-Null

function Levar($origem, $rotulo) {
    if (-not (Test-Path $origem)) { throw "faltando: $rotulo ($origem)" }
    Copy-Item $origem -Destination $Destino -Force
    $mb = [math]::Round((Get-Item $origem).Length / 1MB, 1)
    '{0,8:N1} MB  {1}' -f $mb, (Split-Path $origem -Leaf)
}

Write-Host "`nMontando em $Destino`n" -ForegroundColor Cyan

$itens = @()
$itens += Levar (Join-Path $raiz 'UFC3 Launcher.exe')      'launcher'
$itens += Levar (Join-Path $build 'ufc3.exe')              'jogo recompilado'
foreach ($d in 'rexruntimerd.dll', 'rexgpu-xenosrd.dll', 'TracyClientrd.dll') {
    $itens += Levar (Join-Path $build $d) "runtime ($d)"
}
$itens += Levar (Join-Path $retools 'extract-xiso.exe')    'extrator de ISO'

# Runtime da Microsoft: sem isso o jogo nao abre em maquina sem o Visual Studio.
# So as pastas com numero de versao: existe uma "v143" ao lado que ganharia a
# ordenacao alfabetica e nao tem as DLLs dentro.
$vc = Get-ChildItem $redist -Directory -EA SilentlyContinue |
      Where-Object { $_.Name -match '^\d+\.\d+' } |
      Sort-Object { [version]($_.Name) } -Descending | Select-Object -First 1
if (-not $vc) { throw "redistribuivel do MSVC nao encontrado em $redist" }
foreach ($d in 'msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll') {
    $f = Get-ChildItem $vc.FullName -Recurse -Filter $d -EA SilentlyContinue |
         Where-Object { $_.FullName -like '*\x64\*' } | Select-Object -First 1
    if (-not $f) { throw "faltando no redistribuivel: $d" }
    $itens += Levar $f.FullName "runtime C++ ($d)"
}

$itens | ForEach-Object { "  $_" }

# O launcher.cfg guarda a pasta do jogo de quem rodou aqui: e pessoal e nao vai.
# O ufc3.toml e o oposto -- so tem ajustes de video, audio e controle, nenhum
# caminho -- e TEM que ir junto: sem ele o runtime nao recebe
# gpu_plugin = "xenos", cai em renderizacao nativa e a tela fica preta para
# quem abrir o ufc3.exe direto, sem passar pelo launcher.
foreach ($lixo in 'launcher.cfg', 'ufc3.sha256') {
    Remove-Item (Join-Path $Destino $lixo) -Force -EA SilentlyContinue
}
Get-ChildItem $Destino -Filter *.log -File -Recurse -EA SilentlyContinue | Remove-Item -Force
Remove-Item (Join-Path $Destino 'logs') -Recurse -Force -EA SilentlyContinue
Remove-Item (Join-Path $Destino 'cache') -Recurse -Force -EA SilentlyContinue

# Gera o toml abrindo o launcher sem configuracao: sai exatamente o que uma
# instalacao nova produz, e nao os ajustes de quem empacotou.
Remove-Item (Join-Path $Destino 'ufc3.toml') -Force -EA SilentlyContinue
$l = Start-Process (Join-Path $Destino 'UFC3 Launcher.exe') -WorkingDirectory $Destino -PassThru
for ($i = 0; $i -lt 40 -and -not (Test-Path (Join-Path $Destino 'ufc3.toml')); $i++) {
    Start-Sleep -Milliseconds 250
}
$l.CloseMainWindow() | Out-Null
if (-not $l.WaitForExit(3000)) { $l.Kill() }
Remove-Item (Join-Path $Destino 'launcher.cfg') -Force -EA SilentlyContinue

$toml = Join-Path $Destino 'ufc3.toml'
if (-not (Test-Path $toml)) { throw 'o launcher nao gerou o ufc3.toml' }
if (Select-String -Path $toml -Pattern '[A-Za-z]:\\' -Quiet) {
    throw 'o ufc3.toml gerado contem um caminho de disco; nao pode ser distribuido'
}
if (-not (Select-String -Path $toml -Pattern '^gpu_plugin\s*=\s*"xenos"' -Quiet)) {
    throw 'o ufc3.toml gerado nao tem gpu_plugin = "xenos" -- daria tela preta'
}
Write-Host '  ufc3.toml padrao gerado e conferido (xenos presente, sem caminhos)' -ForegroundColor DarkGray

# Nenhum arquivo do jogo pode ter entrado no pacote.
$proibidos = Get-ChildItem $Destino -Recurse -File |
             Where-Object { $_.Extension -in '.xex', '.iso', '.bik', '.pck', '.bnk' }
if ($proibidos) {
    $proibidos | ForEach-Object { Write-Host "  ARQUIVO DO JOGO NO PACOTE: $($_.Name)" -ForegroundColor Red }
    throw 'o pacote nao pode conter arquivos do disco do jogo'
}

$total = (Get-ChildItem $Destino -Recurse -File | Measure-Object Length -Sum).Sum / 1MB
Write-Host ("`n  total: {0:N0} MB em {1} arquivos" -f $total, (Get-ChildItem $Destino -File).Count) -ForegroundColor Green
Write-Host "  conferido: nenhum arquivo do disco do jogo no pacote`n" -ForegroundColor Green
