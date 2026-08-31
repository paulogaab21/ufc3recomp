# ---------------------------------------------------------------------------
#  crash-loop.ps1 - roda o UFC3 recompilado, identifica o tipo de falha e ja
#  entrega o diagnostico com a acao sugerida.
#
#  Uso:  $env:UFC3_GAME = "<pasta do disco extraido>"
#        .\work\crash-loop.ps1
#        .\work\crash-loop.ps1 -Segundos 60
#        .\work\crash-loop.ps1 -Corrigir      # aplica a correcao sozinho
#
#  Dois tipos de falha, com causas opostas:
#
#   "Call to invalid or unregistered function at 0xA"
#      -> funcao FALTANDO. O scanner nao a descobriu.
#      -> acao: ACRESCENTAR 0xA ao manifesto com o tamanho certo.
#
#   "Unresolved branch from 0xA to 0xB"
#      -> funcao CORTADA. Alguma entrada do manifesto picou uma funcao maior,
#         e o salto de 0xA nao encontra 0xB dentro do mesmo corpo.
#      -> acao: REMOVER do manifesto as entradas da regiao.
# ---------------------------------------------------------------------------
param(
    [int]$Segundos = 35,
    [switch]$Corrigir,
    [string]$Build = "out\build\win-amd64-relwithdebinfo"
)

# ---------------------------------------------------------------------------
#  Caminhos. Nada aqui e fixo na maquina de quem escreveu:
#    UFC3_ROOT   raiz do projeto      (padrao: a pasta que contem tools/)
#    UFC3_GAME   disco extraido       (obrigatoria nos scripts que rodam o jogo)
#    REXSDK_SRC  fonte do ReXGlue SDK (para o objdump de PowerPC)
# ---------------------------------------------------------------------------
$proj = if ($env:UFC3_ROOT) { $env:UFC3_ROOT } else { Split-Path $PSScriptRoot -Parent }
$game = $env:UFC3_GAME

$ErrorActionPreference = "Stop"

$exe     = Join-Path $proj "$Build\ufc3.exe"

$log     = Join-Path $proj "work\crash-loop.log"
$man     = Join-Path $proj "ufc3_manifest.toml"
$base    = Join-Path $proj "work\base.bin"
$objdump = (Join-Path $env:REXSDK_SRC "tools\binutils\powerpc-none-elf-objdump.exe")
$env:CYGWIN = "nodosfilewarning"

if (-not (Test-Path $exe)) { throw "executavel nao encontrado: $exe" }

function Desmonta([uint32]$ini, [uint32]$fim, [uint32]$marca1 = 0, [uint32]$marca2 = 0) {
    & $objdump -D -b binary -m powerpc:common -EB --adjust-vma=0x82000000 `
        --start-address=$ini --stop-address=$fim $base 2>$null |
      Select-Object -Skip 7 | ForEach-Object {
        if ($marca1 -and $_ -match ("^{0:x8}:" -f $marca1)) { Write-Host "  >> $_" -ForegroundColor Green }
        elseif ($marca2 -and $_ -match ("^{0:x8}:" -f $marca2)) { Write-Host "  ->  $_" -ForegroundColor Cyan }
        else { Write-Host "     $_" }
      }
}

Remove-Item $log -ErrorAction SilentlyContinue
Write-Host "`n  rodando ate $Segundos s..." -ForegroundColor Cyan
$p = Start-Process -FilePath $exe `
        -ArgumentList "--game_data_root","`"$game`"","--gpu_plugin","xenos","--log_file","`"$log`"","--log_verbose" `
        -WorkingDirectory (Split-Path $exe) -PassThru
for ($i = 0; $i -lt $Segundos; $i++) {
    if (-not (Get-Process -Id $p.Id -ErrorAction SilentlyContinue)) { break }
    Start-Sleep -Seconds 1
}
if (Get-Process -Id $p.Id -ErrorAction SilentlyContinue) {
    Write-Host "`n  SOBREVIVEU $Segundos s SEM CRASH" -ForegroundColor Green
    Stop-Process -Id $p.Id -Force
    Get-Content $log -Tail 10 | ForEach-Object { "    $_" }
    return
}

$linhas = (Get-Content $log | Measure-Object -Line).Lines
Write-Host "  encerrou apos $linhas linhas de log." -ForegroundColor Yellow

$txt = Get-Content $log -Raw

# --- tipo 1: funcao faltando ------------------------------------------------
if ($txt -match "invalid or unregistered function at guest address 0x([0-9A-Fa-f]{8})") {
    $a = [Convert]::ToUInt32($Matches[1], 16)
    Write-Host ("`n  FUNCAO FALTANDO: 0x{0:X8}" -f $a) -ForegroundColor Red
    Write-Host "  O scanner nao descobriu essa funcao. Ache o fim dela e acrescente."
    Write-Host ("`n  --- 0x{0:X8} .. 0x{1:X8} ---`n" -f ($a - 0x30), ($a + 0xB0))
    Desmonta ($a - 0x30) ($a + 0xB0) $a
    Write-Host ("`n  Acrescente ao manifesto:  0x{0:X8} = {{ size = 0x?? }}" -f $a) -ForegroundColor Cyan
    Write-Host "  (o fim e' o blr final, ou onde comeca a proxima com mflr)"
    return
}

# --- tipo 2: funcao cortada -------------------------------------------------
# "Unresolved branch" e "Unresolved call" sao o mesmo problema visto de dois
# angulos: um salto/chamada cujo destino caiu fora do corpo da funcao. Quando ha
# entradas minhas na regiao, a causa e' corte; quando nao ha, e' a analise do
# proprio rexglue.
if ($txt -match "Unresolved (?:branch|call) from 0x([0-9A-Fa-f]{8}) to 0x([0-9A-Fa-f]{8})") {
    $de   = [Convert]::ToUInt32($Matches[1], 16)
    $para = [Convert]::ToUInt32($Matches[2], 16)
    Write-Host ("`n  FUNCAO CORTADA: salto de 0x{0:X8} para 0x{1:X8} caiu fora do corpo" -f $de, $para) -ForegroundColor Red

    # O salto pode ser para tras (loop, epilogo compartilhado) ou para frente.
    # A funcao real cobre pelo menos de min(de,para) ate max(de,para), entao a
    # janela precisa envolver os dois -- com folga antes, porque a funcao comeca
    # antes do primeiro dos dois.
    $lo = [Math]::Min($de, $para)
    $hi = [Math]::Max($de, $para)
    $ini = $lo - 0x800
    $suspeitas = @()
    foreach ($l in Get-Content $man) {
        if ($l -match "^0x([0-9A-Fa-f]{8}) = \{ size = (0x[0-9A-Fa-f]+) \}") {
            $ea = [Convert]::ToUInt32($Matches[1], 16)
            if ($ea -ge $ini -and $ea -le $hi) { $suspeitas += $l }
        }
    }
    if ($suspeitas) {
        Write-Host "`n  Entradas do manifesto na regiao (provavel causa):" -ForegroundColor Yellow
        $suspeitas | ForEach-Object { "    $_" }
        if ($Corrigir) {
            $conteudo = Get-Content $man
            foreach ($s in $suspeitas) {
                $addr = ($s -split ' ')[0]
                $conteudo = $conteudo | Where-Object { $_ -notmatch "^$([regex]::Escape($addr)) = " }
            }
            Set-Content $man $conteudo
            Write-Host "`n  REMOVIDAS $($suspeitas.Count) entradas." -ForegroundColor Green
            Write-Host "  Agora: rexglue codegen ufc3_manifest.toml && cmake --build $Build"
        } else {
            Write-Host "`n  Rode de novo com -Corrigir para remove-las automaticamente." -ForegroundColor Cyan
        }
    } else {
        Write-Host "`n  Nenhuma entrada minha na regiao -- o corte veio da analise do proprio"
        Write-Host "  rexglue. Precisa investigar no Ghidra."
    }
    Write-Host ("`n  --- 0x{0:X8} .. 0x{1:X8} ---`n" -f ($lo - 0x20), ($lo + 0x60))
    Desmonta ($lo - 0x20) ($lo + 0x60) $de $para
    if ($hi - $lo -gt 0x80) {
        Write-Host ("`n  --- (outro extremo) 0x{0:X8} .. 0x{1:X8} ---`n" -f ($hi - 0x40), ($hi + 0x40))
        Desmonta ($hi - 0x40) ($hi + 0x40) $de $para
    }
    return
}

Write-Host "`n  Falha de outro tipo. Ultimas linhas:" -ForegroundColor Yellow
Get-Content $log -Tail 18 | ForEach-Object { "    $_" }
