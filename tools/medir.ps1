# =============================================================================
#  medir.ps1 - procura o gargalo, medindo todas as camadas ao mesmo tempo.
#
#  A pergunta que isso responde: quem esta segurando o jogo?
#
#    GPU alta + CPU baixa      -> gargalo na GPU (pixels demais)
#    GPU baixa + 1 thread 100% -> gargalo no codigo recompilado (CPU do guest)
#    Textura/s alto            -> cache pequeno, recarga constante
#    Resolve/s < 60            -> a simulacao nao acompanha os vblanks
#
#  Uso:  .\work\medir.ps1 -Segundos 45 -Rotulo "2x2"
# =============================================================================
param(
    [int]$Segundos = 45,
    [string]$Rotulo = "teste"
)

# ---------------------------------------------------------------------------
#  Caminhos. Nada aqui e fixo na maquina de quem escreveu:
#    UFC3_ROOT   raiz do projeto      (padrao: a pasta que contem tools/)
#    UFC3_GAME   disco extraido       (obrigatoria nos scripts que rodam o jogo)
#    REXSDK_SRC  fonte do ReXGlue SDK (para o objdump de PowerPC)
# ---------------------------------------------------------------------------
$proj = if ($env:UFC3_ROOT) { $env:UFC3_ROOT } else { Split-Path $PSScriptRoot -Parent }
$game = $env:UFC3_GAME


$d    = "$proj\out\build\win-amd64-relwithdebinfo"

$log  = "$proj\work\medir_$Rotulo.log"

Get-Process ufc3 -EA SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2
if (Test-Path $log) { Remove-Item -LiteralPath $log -Force }

Write-Host "`n=== $Rotulo ===" -ForegroundColor Cyan
Write-Host "configuracao:"
Get-Content "$d\ufc3.toml" | Where-Object { $_ -match '^(draw_resolution_scale|anisotropic|texture_cache|depth_float24_round|vsync|video_mode_refresh)' } |
    ForEach-Object { "   $_" }

$p = Start-Process -FilePath "$d\ufc3.exe" `
    -ArgumentList "--game_data_root","`"$game`"","--log_file","`"$log`"","--log_verbose" `
    -WorkingDirectory $d -PassThru

Write-Host "`n  aquecendo 15 s (boot, shaders, primeira cena)..." -ForegroundColor DarkGray
Start-Sleep -Seconds 15
if (-not (Get-Process -Id $p.Id -EA SilentlyContinue)) { Write-Host "  encerrou durante o aquecimento" -ForegroundColor Red; return }

# marca o inicio da janela de medicao
$cpu0  = (Get-Process -Id $p.Id).CPU
$linha0 = (Get-Content $log | Measure-Object -Line).Lines
$t0 = Get-Date

# amostra GPU durante a janela
$amostras = @()
$fim = (Get-Date).AddSeconds($Segundos)
while ((Get-Date) -lt $fim) {
    if (-not (Get-Process -Id $p.Id -EA SilentlyContinue)) { break }
    $g = & nvidia-smi --query-gpu=utilization.gpu,utilization.memory,memory.used,clocks.sm `
                      --format=csv,noheader,nounits 2>$null
    if ($g) { $amostras += ,($g -split ',' | ForEach-Object { [double]$_.Trim() }) }
    Start-Sleep -Milliseconds 900
}

$viva = Get-Process -Id $p.Id -EA SilentlyContinue
if (-not $viva) { Write-Host "  o jogo encerrou durante a medicao" -ForegroundColor Red; return }

$cpu1 = $viva.CPU
$t1 = Get-Date
$seg = ($t1 - $t0).TotalSeconds
$nucleos = [Environment]::ProcessorCount

# --- threads mais quentes ---
$topThreads = $viva.Threads | Sort-Object { $_.TotalProcessorTime.TotalSeconds } -Descending |
              Select-Object -First 4

Stop-Process -Id $p.Id -Force
Start-Sleep -Seconds 1

# --- log ---
$txt = Get-Content $log
$resolves = ($txt | Select-String -Pattern 'Resolve: 0,0' -SimpleMatch).Count
$texturas = ($txt | Select-String -Pattern 'Loaded tiled|Loaded linear' ).Count

Write-Host "`n  --- CPU ---" -ForegroundColor Yellow
"   tempo de CPU na janela : {0:N1}s em {1:N1}s de relogio" -f ($cpu1-$cpu0), $seg
"   nucleos ocupados       : {0:N2} de $nucleos" -f (($cpu1-$cpu0)/$seg)
"   threads mais quentes   :"
foreach ($t in $topThreads) { "      tid {0}  {1:N1}s acumulados" -f $t.Id, $t.TotalProcessorTime.TotalSeconds }

Write-Host "`n  --- GPU ---" -ForegroundColor Yellow
if ($amostras.Count -gt 0) {
    $u = $amostras | ForEach-Object { $_[0] }
    $mem = $amostras | ForEach-Object { $_[2] }
    "   uso da GPU   : media {0:N0}%  pico {1:N0}%" -f (($u | Measure-Object -Average).Average, ($u | Measure-Object -Maximum).Maximum)[0], (($u | Measure-Object -Maximum).Maximum)
    "   memoria GPU  : {0:N0} MB" -f (($mem | Measure-Object -Average).Average)
    "   amostras     : $($amostras.Count)"
} else { "   sem amostras de GPU" }

Write-Host "`n  --- trabalho do jogo ---" -ForegroundColor Yellow
"   resolves (quadros do guest) : {0}  = {1:N1}/s" -f $resolves, ($resolves/$seg)
"   texturas carregadas         : {0}  = {1:N1}/s" -f $texturas, ($texturas/$seg)
"   (o alvo e' 60 resolves/s -- abaixo disso a simulacao esta atrasando)"
Write-Host ""
