# ---------------------------------------------------------------------------
#  auto-loop.ps1 - repete o ciclo   rodar -> corrigir -> gerar -> compilar
#
#  So age no caso "funcao cortada" (Unresolved branch), que tem correcao
#  mecanica: remover as entradas do manifesto na regiao. O caso "funcao
#  faltando" exige decidir um tamanho novo -- isso para o loop e pede humano.
#
#  Uso:  $env:UFC3_GAME = "<pasta do disco extraido>"
#        .\work\auto-loop.ps1 -Voltas 6
# ---------------------------------------------------------------------------
param(
    [int]$Voltas = 5,
    [int]$Segundos = 40,
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


Set-Location $proj
$historico = @()

for ($v = 1; $v -le $Voltas; $v++) {
    Write-Host "`n=================== VOLTA $v de $Voltas ===================" -ForegroundColor Magenta
    $n = (Select-String -Path ufc3_manifest.toml -Pattern '^0x').Count
    Write-Host "  manifesto: $n entradas"

    # Write-Host escreve no stream de Informacao (6), nao no pipeline -- sem o
    # *>&1 a saida do script chega vazia aqui.
    $saida = & "$proj\work\crash-loop.ps1" -Segundos $Segundos -Corrigir *>&1 | Out-String
    $linhas = if ($saida -match "encerrou apos (\d+) linhas") { [int]$Matches[1] } else { 0 }

    if ($saida -match "SOBREVIVEU") {
        Write-Host "`n  *** SEM CRASH EM $Segundos SEGUNDOS ***" -ForegroundColor Green
        $historico += [pscustomobject]@{ Volta = $v; Entradas = $n; Linhas = $linhas; Parou = "sem crash" }
        break
    }

    if ($saida -match "FUNCAO FALTANDO: (0x[0-9A-F]{8})") {
        Write-Host "`n  PAROU: funcao faltando em $($Matches[1])" -ForegroundColor Yellow
        Write-Host "  Isso exige decidir um tamanho -- nao da pra automatizar com seguranca."
        Write-Host "  Rode: .\work\crash-loop.ps1   para ver a desmontagem."
        $historico += [pscustomobject]@{ Volta = $v; Entradas = $n; Linhas = $linhas; Parou = "faltando $($Matches[1])" }
        break
    }

    if ($saida -match "REMOVIDAS (\d+) entradas") {
        $rem = $Matches[1]
        $onde = if ($saida -match "salto de (0x[0-9A-F]{8})") { $Matches[1] } else { "?" }
        Write-Host "  cortada em $onde -> removidas $rem entradas" -ForegroundColor Yellow
        $historico += [pscustomobject]@{ Volta = $v; Entradas = $n; Linhas = $linhas; Parou = "cortada $onde (-$rem)" }
    } else {
        Write-Host "`n  Falha de tipo nao tratado. Parando." -ForegroundColor Red
        Write-Host ($saida -split "`n" | Select-Object -Last 15)
        $historico += [pscustomobject]@{ Volta = $v; Entradas = $n; Linhas = $linhas; Parou = "desconhecido" }
        break
    }

    rexglue codegen ufc3_manifest.toml 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host "  CODEGEN FALHOU" -ForegroundColor Red; break }
    cmake --build $Build 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host "  BUILD FALHOU" -ForegroundColor Red; break }
    Write-Host "  regerado e recompilado."
}

Write-Host "`n=================== HISTORICO ===================" -ForegroundColor Magenta
$historico | Format-Table -AutoSize
