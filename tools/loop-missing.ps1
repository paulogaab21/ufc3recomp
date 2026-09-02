# ---------------------------------------------------------------------------
#  loop-missing.ps1 - ciclo automatico para funcoes FALTANDO
#
#  Roda o jogo, pega o endereco do "Call to invalid or unregistered function",
#  calcula o fim pelo grafo de fluxo (work\add_missing.py), acrescenta ao
#  manifesto, regera e recompila. Repete.
#
#  So age nesse caso. Qualquer outro erro -- funcao cortada, falha de heap,
#  crash sem endereco -- ele PARA e devolve para analise humana. Acrescentar
#  funcao faltante e' a direcao segura: com o tamanho certo, nao trunca nada.
#  Remover ou redimensionar mexe em codigo que ja funciona, e isso nao vai para
#  o automatico.
#
#  Uso:  $env:UFC3_GAME = "<pasta do disco extraido>"
#        .\work\loop-missing.ps1 -Voltas 10
# ---------------------------------------------------------------------------
param(
    [int]$Voltas = 10,
    [int]$Segundos = 45,
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


$exe  = "$proj\$Build\ufc3.exe"

$log  = "$proj\work\loop.log"
Set-Location $proj
$hist = @()

for ($v = 1; $v -le $Voltas; $v++) {
    $n = (Select-String -Path ufc3_manifest.toml -Pattern '^0x').Count
    Write-Host "`n===== VOLTA $v/$Voltas  (manifesto: $n entradas) =====" -ForegroundColor Magenta

    if (Test-Path $log) { Remove-Item -LiteralPath $log -Force }
    $p = Start-Process -FilePath $exe `
            -ArgumentList "--game_data_root","`"$game`"","--gpu_plugin","xenos",
                          "--log_file","`"$log`"","--log_verbose" `
            -WorkingDirectory (Split-Path $exe) -PassThru
    for ($i = 0; $i -lt $Segundos; $i++) {
        if (-not (Get-Process -Id $p.Id -ErrorAction SilentlyContinue)) { break }
        Start-Sleep -Seconds 1
    }
    $vivo = Get-Process -Id $p.Id -ErrorAction SilentlyContinue

    $linhas = if (Test-Path $log) { (Get-Content $log | Measure-Object -Line).Lines } else { 0 }
    $quadros = if (Test-Path $log) { (Select-String -Path $log -Pattern 'Resolve: 0,0').Count } else { 0 }

    if ($vivo) {
        Stop-Process -Id $p.Id -Force
        Write-Host "  SOBREVIVEU $Segundos s  |  $linhas linhas  |  $quadros quadros" -ForegroundColor Green
        $hist += [pscustomobject]@{ Volta=$v; Entradas=$n; Linhas=$linhas; Quadros=$quadros; Fim="sem crash" }
        break
    }

    $txt = Get-Content $log -Raw -ErrorAction SilentlyContinue
    if ($txt -match "invalid or unregistered function at guest address (0x[0-9A-Fa-f]{8})") {
        $addr = $Matches[1]
        Write-Host "  faltando $addr  |  $linhas linhas  |  $quadros quadros" -ForegroundColor Yellow
        $r = python work\add_missing.py $addr 2>&1 | Out-String
        Write-Host "  $($r.Trim())"
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  nao consegui calcular o limite -- para aqui." -ForegroundColor Red
            $hist += [pscustomobject]@{ Volta=$v; Entradas=$n; Linhas=$linhas; Quadros=$quadros; Fim="faltando $addr (sem limite)" }
            break
        }
        $hist += [pscustomobject]@{ Volta=$v; Entradas=$n; Linhas=$linhas; Quadros=$quadros; Fim="faltando $addr" }
    }
    elseif ($txt -match "Unresolved (?:branch|call) from (0x[0-9A-Fa-f]{8})") {
        Write-Host "  FUNCAO CORTADA em $($Matches[1]) -- exige analise. Parando." -ForegroundColor Red
        $hist += [pscustomobject]@{ Volta=$v; Entradas=$n; Linhas=$linhas; Quadros=$quadros; Fim="cortada $($Matches[1])" }
        break
    }
    else {
        Write-Host "  falha de outro tipo. Parando." -ForegroundColor Red
        Get-Content $log -Tail 6 | ForEach-Object { "    $_" }
        $hist += [pscustomobject]@{ Volta=$v; Entradas=$n; Linhas=$linhas; Quadros=$quadros; Fim="outro" }
        break
    }

    rexglue codegen ufc3_manifest.toml *> work\cg.log
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  CODEGEN FALHOU:" -ForegroundColor Red
        Get-Content work\cg.log -Tail 8 | ForEach-Object { "    $_" }
        break
    }
    $av = (Select-String -Path work\cg.log -Pattern "Unresolved conditional branch").Count
    if ($av -gt 0) { Write-Host "  ATENCAO: $av avisos de branch nao resolvido" -ForegroundColor Yellow }
    cmake --build $Build *> work\build.log
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  BUILD FALHOU:" -ForegroundColor Red
        Select-String -Path work\build.log -Pattern "error|FAILED" | Select-Object -First 5 | ForEach-Object { "    $($_.Line)" }
        break
    }
    Write-Host "  regerado e recompilado."
}

Write-Host "`n===== HISTORICO =====" -ForegroundColor Magenta
$hist | Format-Table -AutoSize
