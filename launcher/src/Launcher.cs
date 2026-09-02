// =============================================================================
//  UFC 3 Launcher  -- versao compilada
//
//  Compilado com o csc.exe do .NET Framework 4.8 que ja vem no Windows, entao
//  nao ha script solto para editar: o XAML, o codigo e as imagens ficam dentro
//  do .exe como recursos.
//
//  Sobre "ninguem consegue alterar": nenhum binario e' inviolavel. O que este
//  faz e' (a) nao deixar fonte em texto na pasta, (b) conferir a integridade do
//  que lanca, e (c) permitir assinatura digital, para que adulteracao seja
//  trabalhosa e detectavel -- nao impossivel.
// =============================================================================
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Markup;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace UFC3Launcher {

// -----------------------------------------------------------------------------
//  Configuracao persistida
// -----------------------------------------------------------------------------
internal sealed class Cfg {
    public string PastaJogo = "";
    public string Resolucao = "1440p";
    public bool   TelaCheia = true;
    // Ligado por padrao, e nao e so por causa de rasgo de imagem: no SDK o
    // vblank do guest sai de video_mode_refresh_rate quando o vsync esta
    // ligado, e de guest_tick_frequency/1000 quando esta desligado. Como o
    // jogo avanca um passo de simulacao por vblank, desligar poe a simulacao
    // a 1000 Hz e tudo corre cerca de 16x mais rapido.
    public bool   VSync = true;
    public int    Monitor = 0;
    public double TaxaHz = 60.0;
    // Supersampling fica em 1x por padrao e so sobe se o usuario escolher.
    // Em 2x o jogo renderiza com o dobro da dimensao -- quatro vezes os pixels.
    // Aqui isso nao vira "menos fps": a simulacao avanca um passo por vblank a
    // 60 Hz, entao GPU sobrecarregada faz o jogo rodar em camera lenta.
    public int    Superamostragem = 1;
    // Marca que esta configuracao foi escrita DEPOIS de o supersampling voltar a
    // ser escolhivel. Ver a migracao em Carregar: sem isso, um valor salvo antes
    // da trava reviveria sozinho.
    public bool   SuperamostragemLiberada = false;
    // 0 = Medio, 1 = Alto, 2 = Maximo. Define anisotropico, cache de textura e
    // precisao de profundidade. Supersampling NAO entra: fica fixo em 1x.
    public int    Qualidade = 1;

    // draw_resolution_scale: a resolucao INTERNA em que o jogo desenha.
    // O UFC 3 renderiza a 1280x720 e este valor multiplica: 2 = 2560x1440.
    // Limitado a 2 de proposito. Medido numa RTX 3060 Ti durante uma luta:
    //   1x -> GPU ~25%, velocidade correta
    //   2x -> GPU 98% (minimo 93%), a simulacao nao acompanha os vblanks
    // Em 3x (4K) nao ha placa que sustente com folga neste jogo.
    public int    DrawScale = 1;
    public bool   ShaderAsync = true;
    public int    Idioma = 1;
    public int    Pais = 103;
    public bool   Mudo = false;
    public bool   TecladoMouse = false;
    public double Sensibilidade = 1.0;
    public int    CacheTexturaSoft = 1024;   // MB (padrao do SDK: 384)
    public int    CacheTexturaHard = 2048;   // MB (padrao do SDK: 768)

    // ---------------------------------------------------------------------
    //  Renderizador nativo
    //
    //  O caminho normal desenha por emulacao: o jogo escreve pacotes PM4 e o
    //  emulador reconstroi o quadro traduzindo estado do Xenos. O caminho
    //  nativo le as estruturas do proprio jogo e desenha direto por D3D12.
    //
    //  A cena ainda NAO passa por ele -- esta em construcao. O que ja funciona
    //  e' o pos-processamento sobre o quadro pronto, que e' de onde sai o fundo
    //  desfocado do menu.
    // ---------------------------------------------------------------------
    public int    Nitidez = 55;            // por cento, 0 desliga
    public bool   MenuDesfoque = true;
    public int    MenuDesfoqueForca = 100;   // por cento

    // Cadencia de apresentacao.
    //
    //  Isto nao e' so suavidade: a simulacao avanca um passo por vblank, e sem
    //  vsync o vblank do guest ia a 1000 Hz fixos -- o jogo corria cerca de 16
    //  vezes mais rapido. Com o limitador ligado, o vblank passa a seguir a
    //  taxa daqui, e desligar o vsync deixa de quebrar a velocidade do jogo.
    public bool   LimitadorQuadros = false;
    public double LimitadorFps = 120.0;
    // Ritmo por relogio do host com espera precisa, em vez de sondar o relogio
    // do guest em laco. Ajuda quem tem monitor de taxa variavel.
    public bool   RitmoRelogioHost = false;

    // Formato proprio, linha "chave=valor". Evita dependencia de serializador e
    // e' trivial de validar.
    public static Cfg Carregar(string caminho) {
        var c = new Cfg();
        if (!File.Exists(caminho)) return c;
        foreach (var linha in File.ReadAllLines(caminho)) {
            var i = linha.IndexOf('=');
            if (i <= 0) continue;
            string k = linha.Substring(0, i).Trim(), v = linha.Substring(i + 1).Trim();
            var ci = CultureInfo.InvariantCulture;
            switch (k) {
                case "PastaJogo": c.PastaJogo = v; break;
                case "Resolucao": c.Resolucao = v; break;
                case "TelaCheia": c.TelaCheia = v == "1"; break;
                case "VSync": c.VSync = v == "1"; break;
                case "Monitor": int.TryParse(v, out c.Monitor); break;
                case "TaxaHz": double.TryParse(v, NumberStyles.Any, ci, out c.TaxaHz); break;
                case "Superamostragem": int.TryParse(v, out c.Superamostragem); break;
                case "SuperamostragemLiberada": c.SuperamostragemLiberada = v == "1"; break;
                case "Qualidade": int.TryParse(v, out c.Qualidade); break;
                case "DrawScale": int.TryParse(v, out c.DrawScale); break;
                case "ShaderAsync": c.ShaderAsync = v == "1"; break;
                case "Idioma": int.TryParse(v, out c.Idioma); break;
                case "Pais": int.TryParse(v, out c.Pais); break;
                case "Mudo": c.Mudo = v == "1"; break;
                case "TecladoMouse": c.TecladoMouse = v == "1"; break;
                case "Sensibilidade": double.TryParse(v, NumberStyles.Any, ci, out c.Sensibilidade); break;
                case "CacheTexturaSoft": int.TryParse(v, out c.CacheTexturaSoft); break;
                case "CacheTexturaHard": int.TryParse(v, out c.CacheTexturaHard); break;
                case "Nitidez": int.TryParse(v, out c.Nitidez); break;
                case "MenuDesfoque": c.MenuDesfoque = v == "1"; break;
                case "MenuDesfoqueForca": int.TryParse(v, out c.MenuDesfoqueForca); break;
                case "LimitadorQuadros": c.LimitadorQuadros = v == "1"; break;
                case "LimitadorFps": double.TryParse(v, NumberStyles.Any, ci, out c.LimitadorFps); break;
                case "RitmoRelogioHost": c.RitmoRelogioHost = v == "1"; break;
            }
        }

        // Migracao de uma so vez.
        //
        // O supersampling ja foi escolhivel, depois ficou TRAVADO em 1x por um
        // tempo, e agora voltou a ser escolhivel. Quem tinha 2x salvo daquela
        // epoca carregava um valor que ficou dormente durante a trava -- e que
        // acordaria sozinho ao destravar, sem a pessoa ter escolhido nada.
        //
        // O efeito nao seria obvio: nesta recompilacao a simulacao avanca um
        // passo por vblank, entao GPU saturada nao derruba os quadros por
        // segundo, derruba a VELOCIDADE do jogo. A pessoa veria o jogo lento
        // depois de uma atualizacao e nao teria como ligar uma coisa a outra.
        //
        // Entao uma configuracao sem a marca volta para 1x. Quem quiser 2x
        // escolhe de novo, agora com o aviso ao lado.
        if (!c.SuperamostragemLiberada && c.Superamostragem != 1) {
            c.Superamostragem = 1;
            c.DrawScale = 1;
        }
        return c;
    }

    public void Salvar(string caminho) {
        var ci = CultureInfo.InvariantCulture;
        var sb = new StringBuilder();
        sb.AppendLine("PastaJogo=" + PastaJogo);
        sb.AppendLine("Resolucao=" + Resolucao);
        sb.AppendLine("TelaCheia=" + (TelaCheia ? "1" : "0"));
        sb.AppendLine("VSync=" + (VSync ? "1" : "0"));
        sb.AppendLine("Monitor=" + Monitor);
        sb.AppendLine("TaxaHz=" + TaxaHz.ToString(ci));
        sb.AppendLine("Superamostragem=" + Superamostragem);
        sb.AppendLine("SuperamostragemLiberada=1");
        sb.AppendLine("Qualidade=" + Qualidade);
        sb.AppendLine("DrawScale=" + DrawScale);
        sb.AppendLine("ShaderAsync=" + (ShaderAsync ? "1" : "0"));
        sb.AppendLine("Idioma=" + Idioma);
        sb.AppendLine("Pais=" + Pais);
        sb.AppendLine("Mudo=" + (Mudo ? "1" : "0"));
        sb.AppendLine("TecladoMouse=" + (TecladoMouse ? "1" : "0"));
        sb.AppendLine("Sensibilidade=" + Sensibilidade.ToString(ci));
        sb.AppendLine("CacheTexturaSoft=" + CacheTexturaSoft);
        sb.AppendLine("CacheTexturaHard=" + CacheTexturaHard);
        sb.AppendLine("Nitidez=" + Nitidez);
        sb.AppendLine("MenuDesfoque=" + (MenuDesfoque ? "1" : "0"));
        sb.AppendLine("MenuDesfoqueForca=" + MenuDesfoqueForca);
        sb.AppendLine("LimitadorQuadros=" + (LimitadorQuadros ? "1" : "0"));
        sb.AppendLine("LimitadorFps=" + LimitadorFps.ToString(ci));
        sb.AppendLine("RitmoRelogioHost=" + (RitmoRelogioHost ? "1" : "0"));
        File.WriteAllText(caminho, sb.ToString(), Encoding.UTF8);
    }

    // Predefinicoes de qualidade. Cada uma so mexe em opcoes cujo custo e'
    // moderado e previsivel. O supersampling ficou de fora de proposito: em 2x
    // o jogo renderiza quatro vezes os pixels, e como a simulacao avanca um
    // passo por vblank a 60 Hz, GPU sobrecarregada faz o JOGO ficar lento --
    // nao apenas a taxa de quadros cair.
    public struct Preset {
        public int Anisotropico;      // 2=2x, 4=8x, 5=16x
        public int CacheSoft, CacheHard;   // MB
        public bool DepthRound;       // arredonda profundidade float24 (reduz z-fighting)
        public string Nome, Descricao;
    }

    public static Preset[] Presets = new Preset[] {
        new Preset { Nome = "Médio", Anisotropico = 2, CacheSoft = 384, CacheHard = 768,
                     DepthRound = false,
                     Descricao = "Filtragem 2x, cache padrão do SDK. Para placas mais modestas." },
        new Preset { Nome = "Alto", Anisotropico = 4, CacheSoft = 768, CacheHard = 1536,
                     DepthRound = false,
                     Descricao = "Filtragem 8x e cache maior: menos recarga de textura e pop-in." },
        new Preset { Nome = "Máximo", Anisotropico = 5, CacheSoft = 1024, CacheHard = 2048,
                     DepthRound = true,
                     Descricao = "Filtragem 16x, cache amplo e profundidade arredondada (reduz tremulação em sombras). Pede 8 GB de VRAM." },
    };

    public Preset PresetAtual { get { return Presets[Math.Max(0, Math.Min(2, Qualidade))]; } }

    /// Trava em 1 ou 2. Nunca escala nao-quadrada, nunca acima de 2.
    // Sempre 1. Um ufc3.toml de uma versao anterior pode trazer 2 gravado, e
    // sem esta trava ele voltaria a valer sem passar por nenhuma tela.
    // Travado em 1x, e nao e' timidez: nesta recompilacao a simulacao avanca um
    // passo por vblank do guest, entao GPU sobrecarregada NAO derruba os quadros
    // por segundo -- derruba a VELOCIDADE do jogo.
    //
    // Isso foi medido de novo, e caro: liberar a escolha fez o jogo cair de 60
    // para 35 fps numa luta, com a agravante de que o valor 2x estava salvo de
    // uma versao antiga e voltou a valer sozinho, sem ninguem escolher. A pessoa
    // ve o jogo lento depois de uma atualizacao e nao tem como ligar uma coisa a
    // outra.
    //
    // Uma opcao cujo efeito o jogador nao consegue interpretar nao e' opcao: e'
    // armadilha. Fica em 1x.
    public int DrawEfetivo { get { return 1; } }

    // Escreve o ufc3.toml lido pelo runtime. Chave = nome do cvar.
    // A tela de opcoes dentro do jogo usa este mesmo arquivo.
    public string PastaDados = "";

    // Desligar o vsync sem limitador e' a combinacao que poe o vblank do guest a
    // 1000 Hz e faz o jogo correr ~16x mais rapido. Em vez de deixar o jogador
    // cair nisso, o limitador entra sozinho quando o vsync sai.
    public bool LimitadorEfetivo { get { return LimitadorQuadros || !VSync; } }

    // Sem escolha explicita do jogador, segue a taxa do monitor: e' a cadencia
    // que o jogo espera, e a que mantem a velocidade certa.
    public double LimitadorFpsEfetivo {
        get {
            if (LimitadorQuadros && LimitadorFps >= 1.0) return LimitadorFps;
            return TaxaHz >= 1.0 ? TaxaHz : 60.0;
        }
    }

    public void EscreverToml(string caminho) {
        var ci = CultureInfo.InvariantCulture;
        var s = new StringBuilder();
        s.AppendLine("# Gerado pelo UFC 3 Launcher. Chave = nome do cvar do runtime.");
        s.AppendLine("# A tela de opcoes dentro do jogo usa este mesmo arquivo.");
        s.AppendLine();
        s.AppendLine("# --- Video ---");
        s.AppendLine("resolution = \"" + Resolucao + "\"");
        s.AppendLine("fullscreen = " + (TelaCheia ? "true" : "false"));
        s.AppendLine("monitor = " + Monitor);
        s.AppendLine("video_mode_refresh_rate = " + TaxaHz.ToString("0.0", ci));
        s.AppendLine("vsync = " + (VSync ? "true" : "false"));
        s.AppendLine("# Resolucao interna de renderizacao. O jogo desenha a 1280x720 e este");
        s.AppendLine("# valor multiplica. Em 2x sao quatro vezes os pixels, e aqui isso nao");
        s.AppendLine("# derruba os fps: derruba a VELOCIDADE do jogo, porque a simulacao");
        s.AppendLine("# avanca um passo por vblank a 60 Hz.");
        s.AppendLine("# X e Y precisam ser IGUAIS: escala nao-quadrada (2x1) corrompe as");
        s.AppendLine("# texturas dos personagens -- testado.");
        s.AppendLine("draw_resolution_scale_x = " + DrawEfetivo);
        s.AppendLine("draw_resolution_scale_y = " + DrawEfetivo);
        s.AppendLine();
        s.AppendLine("# --- Qualidade: " + PresetAtual.Nome + " ---");
        s.AppendLine("anisotropic_override = " + PresetAtual.Anisotropico);
        s.AppendLine("depth_float24_round = " + (PresetAtual.DepthRound ? "true" : "false"));
        s.AppendLine();
        s.AppendLine("# Cache de textura. O padrao do SDK (384/768 MB) e' conservador para");
        s.AppendLine("# placas modernas; com folga de VRAM o jogo recarrega textura com menos");
        s.AppendLine("# frequencia, o que reduz engasgo e pop-in ao trocar de camera.");
        s.AppendLine("texture_cache_memory_limit_soft = " + PresetAtual.CacheSoft);
        s.AppendLine("texture_cache_memory_limit_hard = " + PresetAtual.CacheHard);
        s.AppendLine("async_shader_compilation = " + (ShaderAsync ? "true" : "false"));
        s.AppendLine();
        s.AppendLine("# --- GPU ---");
        s.AppendLine("gpu_plugin = \"xenos\"");
        s.AppendLine("# O jogo marca alguns descritores de textura como \"invalid type\".");
        s.AppendLine("# E peculiaridade dele, nao erro do emulador -- sem isto as texturas");
        s.AppendLine("# afetadas sao descartadas e a cena fica incompleta.");
        s.AppendLine("gpu_allow_invalid_fetch_constants = true");
        s.AppendLine();
        s.AppendLine("# Leitura de volta do resolve de render-to-texture.");
        s.AppendLine("#");
        s.AppendLine("# A pele do lutador na tela de criacao nao e' uma textura pronta: o jogo");
        s.AppendLine("# a monta desenhando camadas num alvo de render e depois lendo o");
        s.AppendLine("# resultado de volta para a memoria, para us\u00e1-la como textura. Com o");
        s.AppendLine("# padrao \"none\" essa leitura nao acontece, e o que volta e' lixo -- e' o");
        s.AppendLine("# tronco quebrado que aparece no Criar Lutador.");
        s.AppendLine("#");
        s.AppendLine("# \"some\" copia so quando o cache erra, que e' a variante barata; \"full\"");
        s.AppendLine("# copia sempre e sincroniza, corrigindo mais casos e custando bem mais.");
        s.AppendLine("readback_resolve = \"some\"");
        s.AppendLine();
        s.AppendLine("# --- Renderizador nativo ---");
        s.AppendLine("#");
        s.AppendLine("# O caminho normal desenha por emulacao: o jogo escreve pacotes PM4 e o");
        s.AppendLine("# emulador reconstroi o quadro traduzindo estado do Xenos. O caminho");
        s.AppendLine("# nativo le as estruturas do proprio jogo e desenha direto por D3D12.");
        s.AppendLine("#");
        s.AppendLine("# A CENA ainda nao passa por ele -- isso e trabalho em andamento. A");
        s.AppendLine("# IMAGEM FINAL ja passa, em todo quadro: e de la que saem a nitidez e o");
        s.AppendLine("# fundo desfocado do menu.");
        s.AppendLine("#");
        s.AppendLine("# Nao ha chave para desligar. O que se escolhe e a intensidade -- zero");
        s.AppendLine("# inclusive -- nao se o renderizador participa.");
        s.AppendLine("ufc3_nitidez = " + Nitidez);
        s.AppendLine("ufc3_menu_desfoque = " + (MenuDesfoque ? "true" : "false"));
        s.AppendLine("ufc3_menu_desfoque_forca = " + MenuDesfoqueForca);
        s.AppendLine();
        s.AppendLine("# --- Cadencia de apresentacao ---");
        s.AppendLine("#");
        s.AppendLine("# Aqui nao se decide so a suavidade: a simulacao avanca um passo por");
        s.AppendLine("# vblank do guest. Sem vsync esse vblank ia a 1000 Hz FIXOS, e o jogo");
        s.AppendLine("# corria cerca de 16 vezes mais rapido -- era por isso que desligar o");
        s.AppendLine("# vsync quebrava o jogo em vez de apenas soltar os quadros.");
        s.AppendLine("#");
        s.AppendLine("# Com o limitador ligado, o vblank do guest passa a seguir a taxa abaixo.");
        s.AppendLine("d3d12_present_frame_limiter = " + (LimitadorEfetivo ? "true" : "false"));
        s.AppendLine("d3d12_present_frame_limiter_fps = " + LimitadorFpsEfetivo.ToString("0.0", ci));
        s.AppendLine("vblank_no_vsync_use_present_limiter = true");
        s.AppendLine("# Ritmo por relogio do host com espera precisa, em vez de sondar o");
        s.AppendLine("# relogio do guest em laco. Ajuda em monitores de taxa variavel.");
        s.AppendLine("vblank_host_clock_pacing = " + (RitmoRelogioHost ? "true" : "false"));
        s.AppendLine();
        s.AppendLine("# --- Idioma / regiao ---");
        s.AppendLine("user_language = " + Idioma);
        s.AppendLine("user_country = " + Pais);
        s.AppendLine();
        s.AppendLine("# --- Audio ---");
        s.AppendLine("audio_mute = " + (Mudo ? "true" : "false"));
        s.AppendLine();
        s.AppendLine("# --- Controles ---");
        s.AppendLine("mnk_mode = " + (TecladoMouse ? "true" : "false"));
        s.AppendLine("mnk_mouse = " + (TecladoMouse ? "true" : "false"));
        s.AppendLine("mnk_sensitivity = " + Sensibilidade.ToString("0.0#", ci));
        File.WriteAllText(caminho, s.ToString(), Encoding.UTF8);
    }
}

// -----------------------------------------------------------------------------
//  Integridade: confere que o que vamos lancar e' o que esperamos
// -----------------------------------------------------------------------------
internal static class Integridade {
    public static string Sha256(string caminho) {
        using (var fs = File.OpenRead(caminho))
        using (var sha = SHA256.Create())
            return BitConverter.ToString(sha.ComputeHash(fs)).Replace("-", "");
    }

    // Guarda o hash do ufc3.exe na primeira execucao e avisa se mudar depois.
    // Um rebuild legitimo muda o hash -- por isso e' aviso, nao bloqueio.
    public static string Verificar(string exe, string arquivoHash) {
        if (!File.Exists(exe)) return null;
        string atual = Sha256(exe);
        if (!File.Exists(arquivoHash)) { File.WriteAllText(arquivoHash, atual); return null; }
        string guardado = File.ReadAllText(arquivoHash).Trim();
        if (string.Equals(guardado, atual, StringComparison.OrdinalIgnoreCase)) return null;
        File.WriteAllText(arquivoHash, atual);
        return "O executavel do jogo mudou desde a ultima execucao.\n\n" +
               "Esperado: " + guardado.Substring(0, 16) + "...\n" +
               "Atual:    " + atual.Substring(0, 16) + "...\n\n" +
               "Se voce recompilou, isso e' normal.";
    }
}

// -----------------------------------------------------------------------------
//  Janela
// -----------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  Verificacao da imagem de disco.
//
//  Segue o modelo do Dusklight: em vez de acreditar no caminho que a pessoa
//  apontou, o cabecalho do executavel do console e lido, e o launcher diz com
//  todas as letras se aquilo e mesmo UFC Undisputed 3.
//
//  O identificador do titulo fica no cabecalho opcional EXECUTION_INFO
//  (0x00040006) do XEX2. Tudo em big-endian, como no console.
// ---------------------------------------------------------------------------
internal static class Disco {
    public const uint TitleIdUfc3 = 0x5451087Du;

    public enum Estado { Aguardando, Valido, OutroJogo, Invalido, Pendente }

    public struct Resultado { public Estado Estado; public string Rotulo, Detalhe; }

    static uint LerBE(byte[] b, uint i) {
        return ((uint)b[i] << 24) | ((uint)b[i + 1] << 16) | ((uint)b[i + 2] << 8) | b[i + 3];
    }

    static Resultado Diz(Estado e, string rotulo, string detalhe) {
        var r = new Resultado(); r.Estado = e; r.Rotulo = rotulo; r.Detalhe = detalhe; return r;
    }

    public static Resultado Verificar(string origem) {
        if (string.IsNullOrWhiteSpace(origem))
            return Diz(Estado.Aguardando, "Nenhuma origem selecionada",
                       "Escolha a ISO do seu disco, ou uma pasta ja extraida.");

        // Numa ISO o executavel esta dentro do sistema de arquivos do disco;
        // a conferencia acontece logo depois da extracao, que e a etapa 1.
        if (File.Exists(origem) && origem.EndsWith(".iso", StringComparison.OrdinalIgnoreCase))
            return Diz(Estado.Pendente, "Sera verificado depois da extracao",
                       Path.GetFileName(origem));

        var xex = Directory.Exists(origem) ? Path.Combine(origem, "default.xex") : origem;
        if (!File.Exists(xex))
            return Diz(Estado.Invalido, "default.xex nao encontrado",
                       "Esta pasta nao parece ser a de um disco de Xbox 360.");
        return VerificarXex(xex);
    }

    public static Resultado VerificarXex(string xex) {
        try {
            byte[] cab;
            using (var f = File.OpenRead(xex)) {
                cab = new byte[(int)Math.Min(0x40000L, f.Length)];
                f.Read(cab, 0, cab.Length);
            }
            if (cab.Length < 0x18 || cab[0] != (byte)'X' || cab[1] != (byte)'E'
                                  || cab[2] != (byte)'X' || cab[3] != (byte)'2')
                return Diz(Estado.Invalido, "Nao e um executavel de Xbox 360",
                           "O arquivo nao comeca com a assinatura XEX2.");

            uint n = LerBE(cab, 0x14);
            for (uint i = 0; i < n && 0x18u + i * 8 + 8 <= (uint)cab.Length; i++) {
                uint chave = LerBE(cab, 0x18u + i * 8);
                uint valor = LerBE(cab, 0x18u + i * 8 + 4);
                if (chave != 0x00040006u) continue;
                if (valor + 0x18u > (uint)cab.Length) break;

                uint media  = LerBE(cab, valor);
                uint versao = LerBE(cab, valor + 4);
                uint titulo = LerBE(cab, valor + 0x0C);
                int disco = cab[valor + 0x12], ndiscos = cab[valor + 0x13];

                if (titulo != TitleIdUfc3)
                    return Diz(Estado.OutroJogo, "Este disco nao e UFC Undisputed 3",
                        string.Format("Title ID {0:X8}; o esperado e {1:X8}.", titulo, TitleIdUfc3));

                return Diz(Estado.Valido, "Disco reconhecido \u2014 UFC Undisputed 3",
                    string.Format("Title ID {0:X8} \u00b7 media {1:X8} \u00b7 disco {2} de {3} \u00b7 versao {4}.{5}.{6}.{7}",
                        titulo, media, disco, ndiscos,
                        (versao >> 28) & 0xF, (versao >> 24) & 0xF, (versao >> 8) & 0xFFFF, versao & 0xFF));
            }
            return Diz(Estado.Invalido, "Cabecalho do XEX incompleto",
                       "Nao foi possivel ler a identificacao do titulo.");
        } catch (Exception e) {
            return Diz(Estado.Invalido, "Falha ao ler o executavel", e.Message);
        }
    }

    // Cores herdadas do Dusklight, um tom por estado.
    public static string Cor(Estado e) {
        switch (e) {
            case Estado.Valido:    return "#D8F999";
            case Estado.OutroJogo: return "#FFD6A7";
            case Estado.Invalido:  return "#FFC9C9";
            case Estado.Pendente:  return "#FEE685";
            default:               return "#9A968B";
        }
    }
}


// ---------------------------------------------------------------------------
//  Onde estao as ferramentas de compilacao.
//
//  Nada aqui depende da maquina de quem escreveu o projeto. Cada ferramenta e
//  procurada em tres lugares, nesta ordem:
//
//    1. a pasta que o proprio launcher administra, em %LOCALAPPDATA%
//    2. o PATH do sistema
//    3. a instalacao do Visual Studio / Build Tools
//
//  O passo 3 e o que faz a diferenca: o Build Tools ja traz clang, lld-link,
//  cmake, ninja e o Windows SDK. E ele e obrigatorio de qualquer forma, porque
//  o executavel do jogo e linkado no ABI da Microsoft. Com ele instalado, nao
//  sobra nada para a pessoa instalar a mao.
// ---------------------------------------------------------------------------
internal static class Ferramentas {
    public sealed class Item {
        public string Nome;       // como aparece na tela
        public string Exe;        // nome do executavel
        public string Caminho;    // resolvido, ou null
        public string Origem;     // onde foi achado
        public bool   Achado { get { return !string.IsNullOrEmpty(Caminho); } }
    }

    public static string PastaGerida {
        get {
            return Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "ufc3recomp", "ferramentas");
        }
    }

    // Edicoes do Visual Studio 2022 presentes na maquina.
    static List<string> RaizesVS() {
        var r = new List<string>();
        foreach (var v in new[] { "ProgramFiles", "ProgramFiles(x86)" }) {
            var b = Environment.GetEnvironmentVariable(v);
            if (string.IsNullOrEmpty(b)) continue;
            var vs = Path.Combine(b, "Microsoft Visual Studio", "2022");
            if (!Directory.Exists(vs)) continue;
            try { r.AddRange(Directory.GetDirectories(vs)); } catch { }
        }
        return r;
    }

    static string NoPath(string exe) {
        var p = Environment.GetEnvironmentVariable("PATH");
        if (p == null) return null;
        foreach (var d in p.Split(';')) {
            if (string.IsNullOrWhiteSpace(d)) continue;
            try {
                var f = Path.Combine(d.Trim(), exe);
                if (File.Exists(f)) return f;
            } catch { }
        }
        return null;
    }

    // relativosVS: caminhos dentro de uma edicao do Visual Studio.
    static Item Achar(string nome, string exe, string[] relativosGeridos, string[] relativosVS) {
        var it = new Item(); it.Nome = nome; it.Exe = exe;

        foreach (var rel in relativosGeridos) {
            var f = Path.Combine(PastaGerida, rel);
            if (File.Exists(f)) { it.Caminho = f; it.Origem = "baixado pelo launcher"; return it; }
        }
        var p = NoPath(exe);
        if (p != null) { it.Caminho = p; it.Origem = "PATH do sistema"; return it; }

        foreach (var vs in RaizesVS())
            foreach (var rel in relativosVS) {
                var f = Path.Combine(vs, rel);
                if (File.Exists(f)) { it.Caminho = f; it.Origem = "Visual Studio Build Tools"; return it; }
            }
        return it;
    }

    public static List<Item> Inventario() {
        var l = new List<Item>();
        l.Add(Achar("Compilador (clang++ 18 ou mais novo)", "clang++.exe",
                    new[] { @"llvm\bin\clang++.exe" },
                    new[] { @"VC\Tools\Llvm\x64\bin\clang++.exe" }));
        l.Add(Achar("CMake", "cmake.exe",
                    new[] { @"cmake\bin\cmake.exe" },
                    new[] { @"Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" }));
        l.Add(Achar("Ninja", "ninja.exe",
                    new[] { @"ninja\ninja.exe" },
                    new[] { @"Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" }));
        l.Add(Achar("Tradutor ReXGlue", "rexglue.exe",
                    new[] { @"rexglue\bin\rexglue.exe", @"rexglue\rexglue.exe" },
                    new string[0]));
        l.Add(Achar("extract-xiso", "extract-xiso.exe",
                    new[] { @"extract-xiso\extract-xiso.exe" },
                    new string[0]));
        return l;
    }

    // O Windows SDK e separado: e dele que saem os .lib do link.
    public static bool TemWindowsSdk() {
        foreach (var v in new[] { "ProgramFiles(x86)", "ProgramFiles" }) {
            var b = Environment.GetEnvironmentVariable(v);
            if (string.IsNullOrEmpty(b)) continue;
            var lib = Path.Combine(b, "Windows Kits", "10", "Lib");
            if (Directory.Exists(lib)) {
                try { if (Directory.GetDirectories(lib).Length > 0) return true; } catch { }
            }
        }
        return false;
    }

    // Duas origens diferentes, e a mensagem na tela precisa distinguir:
    //   - clang, cmake, ninja e o Windows SDK vem do Build Tools da Microsoft
    //   - rexglue e extract-xiso sao do proprio projeto, o launcher os fornece
    static readonly string[] DoBuildTools = { "clang++.exe", "cmake.exe", "ninja.exe" };

    public static void Faltando(List<Item> inv, out List<string> buildTools, out List<string> projeto) {
        buildTools = new List<string>();
        projeto    = new List<string>();
        foreach (var i in inv) {
            if (i.Achado) continue;
            if (Array.IndexOf(DoBuildTools, i.Exe) >= 0) buildTools.Add(i.Nome);
            else projeto.Add(i.Nome);
        }
        if (!TemWindowsSdk()) buildTools.Add("Windows SDK");
    }
}

internal sealed class Janela {
    // O launcher trabalha de dois jeitos, decididos por um unico teste: existe
    // um ufc3.exe ao lado dele?
    //
    //   modo pronto    Existe. Nada e compilado: a pessoa aponta o disco, o
    //                  launcher extrai e ja da para jogar. E assim que o
    //                  ufc3Recomp e distribuido -- sem compilador, sem cmake,
    //                  sem nada para instalar.
    //
    //   modo bancada   Nao existe, mas ha o projeto com o codigo. O launcher
    //                  traduz o executavel do console e compila. E o modo de
    //                  quem desenvolve o projeto.
    static readonly string RaizApp =
        Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
    public static readonly bool ModoPronto = File.Exists(Path.Combine(RaizApp, "ufc3.exe"));

    static readonly string Bancada  = @"C:\Users\Gabriel\projects";
    static readonly string Projeto  = Path.Combine(Bancada, "UFC3Recomp");
    static readonly string BuildDir = Path.Combine(Projeto, @"out\build\win-amd64-relwithdebinfo");
    static readonly string Exe      = ModoPronto ? Path.Combine(RaizApp, "ufc3.exe")
                                                 : Path.Combine(BuildDir, "ufc3.exe");
    // O runtime le o ufc3.toml da pasta do proprio executavel. No modo pronto
    // isso e a pasta do launcher; no modo bancada, a pasta de build. Errar isso
    // faz o jogo subir sem gpu_plugin = "xenos" e a tela ficar preta.
    static readonly string Toml     = Path.Combine(Path.GetDirectoryName(Exe), "ufc3.toml");
    // No pacote pronto, saves, cache e logs ficam ao lado do launcher. Nunca
    // podemos levar para outra maquina o caminho absoluto da bancada do
    // desenvolvedor. No modo de desenvolvimento eles continuam fora de
    // out/build, para um rebuild nao apagar o progresso local.
    public static readonly string PastaDados = ModoPronto
        ? Path.Combine(RaizApp, "userdata")
        : Path.Combine(Projeto, "userdata");

    readonly string _raiz;
    readonly string _cfgPath;
    readonly string _hashPath;
    Cfg _cfg;

    static readonly string[] Idiomas = {
        "1 - Ingles", "2 - Japones", "3 - Alemao", "4 - Frances", "5 - Espanhol",
        "6 - Italiano", "7 - Coreano", "8 - Chines tradicional", "9 - Portugues",
        "10 - Chines simplificado", "11 - Polones", "12 - Russo"
    };

    public readonly Window Win;

    public Janela() {
        _raiz = Path.GetDirectoryName(Assembly.GetExecutingAssembly().Location);
        _cfgPath  = Path.Combine(_raiz, "launcher.cfg");
        _hashPath = Path.Combine(_raiz, "ufc3.sha256");

        // XAML embutido no proprio .exe -- nao ha arquivo de layout na pasta
        // para alguem editar.
        using (var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("Interface.xaml"))
            Win = (Window)XamlReader.Load(s);

        CarregarFontes();
        _cfg = Cfg.Carregar(_cfgPath);
        Montar();
    }

    object Achar(string nome) { return Win.FindName(nome); }
    void Close() { Win.Close(); }
    void DragMove() { Win.DragMove(); }
    System.Windows.Threading.Dispatcher Dispatcher { get { return Win.Dispatcher; } }

    void Montar() {
        CarregarImagem("ImgFundo", "art.jpg");
        CarregarImagem("ImgQr", "qrcode.jpg");

        var cbIdioma = (ComboBox)Achar("CbIdioma");
        foreach (var s in Idiomas) cbIdioma.Items.Add(s);
        var cbRes = (ComboBox)Achar("CbResolucao");
        foreach (var s in new[] { "720p", "1080p", "1440p", "4k" }) cbRes.Items.Add(s);
        // A partir daqui o launcher so oferece 1x.
        //
        // O 2x parecia uma escolha de qualidade e nao era: nesta recompilacao a
        // simulacao avanca um passo por vblank, entao quando a GPU nao fecha o
        // quadro a tempo o jogo nao perde quadros -- ele fica em camara lenta.
        // Quem escolhia 2x achava que ganhava nitidez e ganhava um jogo mais
        // devagar, sem nada na tela explicando o porque.
        //
        // Uma opcao que so tem um jeito certo de ser usada nao e' opcao. Fica a
        // caixa, desabilitada, mostrando a resolucao em que o jogo desenha --
        // que continua sendo informacao util.
        // Mostrada, mas nao escolhivel: a resolucao de desenho continua sendo
        // informacao util, e travar a caixa diz mais do que esconde-la.
        var cbDraw = (ComboBox)Achar("CbDraw");
        cbDraw.Items.Add("1x  \u2014  1280x720 (nativo do console)");
        cbDraw.IsEnabled = false;
        cbDraw.SelectionChanged += (a, b) => AtualizarDicaDraw();

        ((Slider)Achar("SldNitidez")).ValueChanged += (a, b) => AtualizarDicaNitidez();
        ((Slider)Achar("SldDesfoque")).ValueChanged += (a, b) => AtualizarDicaDesfoque();
        ((CheckBox)Achar("ChkLimitador")).Checked += (a, b) => AtualizarDicaLimitador();
        ((CheckBox)Achar("ChkLimitador")).Unchecked += (a, b) => AtualizarDicaLimitador();
        ((CheckBox)Achar("ChkVSync")).Checked += (a, b) => AtualizarDicaLimitador();
        ((CheckBox)Achar("ChkVSync")).Unchecked += (a, b) => AtualizarDicaLimitador();

        var cbQual = (ComboBox)Achar("CbQualidade");
        foreach (var pr in Cfg.Presets) cbQual.Items.Add(pr.Nome);
        cbQual.SelectionChanged += (a, b) => AtualizarDicaQualidade();

        ConfigParaUI();

        ((Button)Achar("BtnJogar")).Click += (a, b) => Jogar();
        ((Button)Achar("BtnConfig")).Click += (a, b) => Mostrar("PainelConfig");
        ((Button)Achar("BtnVoltar")).Click += (a, b) => { UIParaConfig(); Salvar(); Mostrar("PainelPrincipal"); };
        ((Button)Achar("BtnReconstruir")).Click += (a, b) => Mostrar("PainelSetup");
        ((Button)Achar("BtnApoiar")).Click += (a, b) => Mostrar("PainelApoiar");
        ((Button)Achar("BtnVoltarApoiar")).Click += (a, b) => Mostrar("PainelPrincipal");
        ((Button)Achar("BtnSair")).Click += (a, b) => Close();
        ((Button)Achar("BtnEscolherIso")).Click += (a, b) => EscolherArquivo();
        ((Button)Achar("BtnEscolherPasta")).Click += (a, b) => EscolherPasta();
        ((Button)Achar("BtnInstalar")).Click += async (a, b) => await Instalar();
        ((System.Windows.Controls.Grid)Achar("BarraTitulo")).MouseLeftButtonDown += (a, b) => DragMove();
        ((Button)Achar("BtnGitHub")).Click += (a, b) => {
            try { Process.Start(new ProcessStartInfo(
                "https://github.com/paulogaab21/ufc3recomp") { UseShellExecute = true }); }
            catch { /* sem navegador padrao: ignora em silencio */ }
        };

        var caixa = (TextBox)Achar("TxtPasta");
        caixa.TextChanged += (a, b) => AtualizarDisco();
        if (!string.IsNullOrEmpty(_cfg.PastaJogo)) caixa.Text = _cfg.PastaJogo;
        AjustarTextosDoModo();
        AtualizarDisco();
        AtualizarFerramentas();

        // Numa instalacao nova ainda nao ha ufc3.toml ao lado do jogo. Sem ele
        // o runtime cai no modo de renderizacao nativa e a tela fica preta,
        // entao ele e escrito ja na primeira abertura do launcher.
        if (!File.Exists(Toml)) Salvar();

        // No modo pronto o executavel ja vem junto; o que falta e o disco.
        bool pronto = ModoPronto
            ? (File.Exists(Exe) && PastaJogoValida())
            : (File.Exists(Exe) && File.Exists(Path.Combine(Projeto, @"assets\game\default.xex")));
        Mostrar(pronto ? "PainelPrincipal" : "PainelSetup");
    }

    // As fontes vao embutidas no exe, mas o WPF so as carrega a partir de um
    // arquivo -- entao sao gravadas uma vez em %LOCALAPPDATA% e lidas de la.
    // Trocar esses arquivos muda so a tipografia, nunca o comportamento.
    static readonly string[] Fontes = {
        "FiraSans-Regular.ttf", "FiraSansCondensed-Regular.ttf", "FiraSansCondensed-Bold.ttf"
    };

    void CarregarFontes() {
        try {
            var dir = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "ufc3recomp", "fontes");
            Directory.CreateDirectory(dir);
            var asm = Assembly.GetExecutingAssembly();
            foreach (var f in Fontes) {
                var destino = Path.Combine(dir, f);
                using (var s = asm.GetManifestResourceStream(f)) {
                    if (s == null) continue;
                    if (File.Exists(destino) && new FileInfo(destino).Length == s.Length) continue;
                    using (var o = File.Create(destino)) s.CopyTo(o);
                }
            }
            var b = new Uri(dir + Path.DirectorySeparatorChar);
            Win.Resources["FonteTexto"]       = new FontFamily(b, "./#Fira Sans");
            Win.Resources["FonteCond"]        = new FontFamily(b, "./#Fira Sans Condensed");
            Win.Resources["FonteCondNegrito"] = new FontFamily(b, "./#Fira Sans Condensed");
        } catch {
            // sem as fontes o launcher abre igual, so com a tipografia do sistema
        }
    }

    void CarregarImagem(string nome, string recurso) {
        var img = Achar(nome) as System.Windows.Controls.Image;
        if (img == null) return;
        using (var s = Assembly.GetExecutingAssembly().GetManifestResourceStream(recurso)) {
            if (s == null) return;
            var bmp = new BitmapImage();
            bmp.BeginInit(); bmp.StreamSource = s; bmp.CacheOption = BitmapCacheOption.OnLoad; bmp.EndInit();
            img.Source = bmp;
        }
    }

    void Mostrar(string qual) {
        foreach (var p in new[] { "PainelSetup", "PainelPrincipal", "PainelConfig", "PainelApoiar" }) {
            var g = Achar(p) as UIElement;
            if (g != null) g.Visibility = p == qual ? Visibility.Visible : Visibility.Collapsed;
        }
        // a arte fica nitida so na tela principal; atras de texto ela atrapalha
        var veu = Achar("VeuPainel") as UIElement;
        if (veu != null) veu.Opacity = (qual == "PainelPrincipal") ? 0.0 : 1.0;
        if (qual == "PainelPrincipal") AtualizarSituacao();
    }

    // Linha de situacao no canto inferior esquerdo da tela principal.
    void AtualizarSituacao() {
        var res = Achar("TxtResumo") as TextBlock;
        if (res != null)
            res.Text = string.Format("{0} · supersampling {1}x · qualidade: {2}",
                _cfg.Resolucao, _cfg.DrawEfetivo, _cfg.PresetAtual.Nome.ToLowerInvariant());
    }

    void ConfigParaUI() {
        ((ComboBox)Achar("CbResolucao")).SelectedItem = _cfg.Resolucao;
        ((CheckBox)Achar("ChkTelaCheia")).IsChecked = _cfg.TelaCheia;
        ((CheckBox)Achar("ChkVSync")).IsChecked = _cfg.VSync;
        ((TextBox)Achar("TxtHz")).Text = _cfg.TaxaHz.ToString(CultureInfo.InvariantCulture);
        ((ComboBox)Achar("CbDraw")).SelectedIndex = 0;
        ((ComboBox)Achar("CbQualidade")).SelectedIndex = Math.Max(0, Math.Min(2, _cfg.Qualidade));
        AtualizarDicaDraw();
        AtualizarDicaQualidade();
        ((CheckBox)Achar("ChkShaderAsync")).IsChecked = _cfg.ShaderAsync;
        ((ComboBox)Achar("CbIdioma")).SelectedIndex = Math.Max(0, _cfg.Idioma - 1);
        ((CheckBox)Achar("ChkMudo")).IsChecked = _cfg.Mudo;
        ((CheckBox)Achar("ChkTecMouse")).IsChecked = _cfg.TecladoMouse;
        ((Slider)Achar("SldSens")).Value = _cfg.Sensibilidade;
        ((CheckBox)Achar("ChkMenuDesfoque")).IsChecked = _cfg.MenuDesfoque;
        ((Slider)Achar("SldNitidez")).Value = _cfg.Nitidez;
        ((Slider)Achar("SldDesfoque")).Value = _cfg.MenuDesfoqueForca;
        ((CheckBox)Achar("ChkLimitador")).IsChecked = _cfg.LimitadorQuadros;
        ((TextBox)Achar("TxtLimitadorFps")).Text =
            _cfg.LimitadorFps.ToString(CultureInfo.InvariantCulture);
        ((CheckBox)Achar("ChkRitmoHost")).IsChecked = _cfg.RitmoRelogioHost;
        AtualizarDicaNitidez();
        AtualizarDicaDesfoque();
        AtualizarDicaLimitador();
    }

    void UIParaConfig() {
        var ci = CultureInfo.InvariantCulture;
        var r = ((ComboBox)Achar("CbResolucao")).SelectedItem as string;
        if (!string.IsNullOrEmpty(r)) _cfg.Resolucao = r;
        _cfg.TelaCheia = ((CheckBox)Achar("ChkTelaCheia")).IsChecked == true;
        _cfg.VSync = ((CheckBox)Achar("ChkVSync")).IsChecked == true;
        double hz; if (double.TryParse(((TextBox)Achar("TxtHz")).Text, NumberStyles.Any, ci, out hz)) _cfg.TaxaHz = hz;
        _cfg.Superamostragem = 1;
        _cfg.DrawScale = 1;
        int q = ((ComboBox)Achar("CbQualidade")).SelectedIndex;
        _cfg.Qualidade = (q >= 0 && q <= 2) ? q : 1;
        _cfg.ShaderAsync = ((CheckBox)Achar("ChkShaderAsync")).IsChecked == true;
        _cfg.Idioma = ((ComboBox)Achar("CbIdioma")).SelectedIndex + 1;
        _cfg.Mudo = ((CheckBox)Achar("ChkMudo")).IsChecked == true;
        _cfg.TecladoMouse = ((CheckBox)Achar("ChkTecMouse")).IsChecked == true;
        _cfg.Sensibilidade = ((Slider)Achar("SldSens")).Value;
        _cfg.MenuDesfoque = ((CheckBox)Achar("ChkMenuDesfoque")).IsChecked == true;
        _cfg.Nitidez = (int)Math.Round(((Slider)Achar("SldNitidez")).Value);
        _cfg.MenuDesfoqueForca = (int)Math.Round(((Slider)Achar("SldDesfoque")).Value);
        _cfg.LimitadorQuadros = ((CheckBox)Achar("ChkLimitador")).IsChecked == true;
        double fps;
        if (double.TryParse(((TextBox)Achar("TxtLimitadorFps")).Text, NumberStyles.Any, ci, out fps)
            && fps >= 1.0 && fps <= 1000.0) {
            _cfg.LimitadorFps = fps;
        }
        _cfg.RitmoRelogioHost = ((CheckBox)Achar("ChkRitmoHost")).IsChecked == true;
    }

    void AtualizarDicaNitidez() {
        var s = Achar("SldNitidez") as Slider;
        var t = Achar("TxtNitidezDica") as TextBlock;
        if (s == null || t == null) return;
        int v = (int)Math.Round(s.Value);
        t.Text = v == 0 ? "0% \u2014 desligada, imagem como sai da emula\u00e7\u00e3o"
               : v <= 35 ? v + "% \u2014 discreta"
               : v <= 70 ? v + "%"
               : v + "% \u2014 forte, pode marcar as bordas";
    }

    void AtualizarDicaDesfoque() {
        var s = Achar("SldDesfoque") as Slider;
        var t = Achar("TxtDesfoqueDica") as TextBlock;
        if (s == null || t == null) return;
        int v = (int)Math.Round(s.Value);
        t.Text = v == 0 ? "0% — só escurece, sem desfocar"
               : v <= 60 ? v + "% — desfoque leve"
               : v <= 150 ? v + "%"
               : v + "% — bem forte";
    }

    // O aviso muda de acordo com o VSync porque a combinacao importa: sem vsync
    // e sem limitador, o vblank do guest vai a 1000 Hz e o jogo corre rapido
    // demais. Nesse caso o launcher liga o limitador sozinho, e a dica diz isso.
    void AtualizarDicaLimitador() {
        var chk = Achar("ChkLimitador") as CheckBox;
        var t = Achar("TxtLimitadorDica") as TextBlock;
        var vsync = Achar("ChkVSync") as CheckBox;
        var caixa = Achar("TxtLimitadorFps") as TextBox;
        if (chk == null || t == null || vsync == null) return;

        bool semVSync = vsync.IsChecked != true;
        bool ligado = chk.IsChecked == true;
        if (caixa != null) caixa.IsEnabled = ligado;

        if (semVSync && !ligado) {
            t.Text = "Com o VSync desligado o limitador entra sozinho, na taxa do monitor. " +
                     "Sem ele a simulação do jogo dispararia para 1000 Hz.";
        } else if (ligado) {
            t.Text = "Entrega os quadros num compasso regular, em vez de assim que ficam prontos. " +
                     "A simulação segue essa mesma taxa.";
        } else {
            t.Text = "Entrega os quadros num compasso regular, em vez de assim que ficam prontos.";
        }
    }

    void AtualizarDicaDraw() {
        var cb = Achar("CbDraw") as ComboBox;
        var t = Achar("TxtDrawDica") as TextBlock;
        if (cb == null || t == null) return;
        t.Text = "O jogo desenha a 720p, o nativo do Xbox 360, e a imagem é ampliada para a " +
                 "resolução da tela — a nitidez do renderizador nativo recupera boa parte da " +
                 "definição. Resoluções maiores ficaram travadas: elas não derrubam os quadros " +
                 "por segundo, derrubam a velocidade do jogo, porque a simulação avança um " +
                 "passo por vblank.";
    }

    void AtualizarDicaQualidade() {
        var cb = Achar("CbQualidade") as ComboBox;
        var t = Achar("TxtQualidadeDica") as TextBlock;
        if (cb == null || t == null) return;
        int i = Math.Max(0, Math.Min(2, cb.SelectedIndex));
        t.Text = Cfg.Presets[i].Descricao;
    }

    void Salvar() {
        _cfg.PastaDados = PastaDados;
        Directory.CreateDirectory(PastaDados);
        _cfg.Salvar(_cfgPath);
        // Testa a pasta real do executavel, nao o BuildDir fixo da bancada.
        // No pacote distribuido o ufc3.exe fica ao lado do launcher e tambem
        // precisa receber o TOML; sem gpu_plugin = "xenos" a tela fica preta.
        string pastaExe = Path.GetDirectoryName(Exe);
        if (!string.IsNullOrEmpty(pastaExe) && Directory.Exists(pastaExe))
            _cfg.EscreverToml(Toml);
    }

    void EscolherArquivo() {
        var d = new Microsoft.Win32.OpenFileDialog {
            Filter = "Imagem de disco (*.iso)|*.iso|Todos os arquivos|*.*",
            Title = "Selecione a ISO do UFC Undisputed 3"
        };
        if (d.ShowDialog() == true) ((TextBox)Achar("TxtPasta")).Text = d.FileName;
    }

    void EscolherPasta() {
        // Sem WinForms: usa o dialogo de arquivo apontando para qualquer item da
        // pasta, e fica com o diretorio.
        var d = new Microsoft.Win32.OpenFileDialog {
            Title = "Entre na pasta do jogo e selecione o default.xex",
            Filter = "default.xex|default.xex|Todos os arquivos|*.*",
            CheckFileExists = false
        };
        if (d.ShowDialog() == true)
            ((TextBox)Achar("TxtPasta")).Text = Path.GetDirectoryName(d.FileName);
    }

    // No modo pronto nao ha compilacao, entao a tela nao pode falar dela.
    void AjustarTextosDoModo() {
        if (!ModoPronto) return;

        var t = Achar("TxtSetupTitulo") as TextBlock;
        if (t != null) t.Text = "Aponte o seu disco";
        var d = Achar("TxtSetupDescricao") as TextBlock;
        if (d != null) d.Text = "Escolha a ISO do seu disco de UFC Undisputed 3, ou uma pasta "
                              + "j\u00e1 extra\u00edda. O launcher extrai e o jogo abre \u2014 n\u00e3o "
                              + "\u00e9 preciso compilar nada nem instalar nenhuma ferramenta.";
        var b = Achar("BtnInstalar") as Button;
        if (b != null) b.Content = "EXTRAIR E JOGAR";

        // A linha de ferramentas so faz sentido em quem compila.
        foreach (var n in new[] { "PontoFerr", "TxtFerrRotulo", "TxtFerrDetalhe" }) {
            var e = Achar(n) as UIElement;
            if (e != null) e.Visibility = Visibility.Collapsed;
        }
    }

    // Diz se da para compilar nesta maquina, e de onde vem cada ferramenta.
    void AtualizarFerramentas() {
        if (ModoPronto) return;          // nada a compilar, nada a exigir
        var inv = Ferramentas.Inventario();
        List<string> deBT, doProjeto;
        Ferramentas.Faltando(inv, out deBT, out doProjeto);

        string cor, rotulo, detalhe;
        if (deBT.Count == 0 && doProjeto.Count == 0) {
            cor = "#D8F999";
            rotulo = "Ferramentas de compilacao prontas";
            var origens = new List<string>();
            foreach (var i in inv)
                if (i.Achado && !origens.Contains(i.Origem)) origens.Add(i.Origem);
            detalhe = "clang, cmake, ninja e Windows SDK localizados \u2014 "
                    + string.Join("; ", origens.ToArray()) + ".";
        } else if (deBT.Count > 0) {
            cor = "#FFD6A7";
            rotulo = "Falta o compilador: " + string.Join(", ", deBT.ToArray());
            detalhe = "Isso vem do Visual Studio Build Tools, gratuito, com a carga "
                    + "\"Desenvolvimento para desktop com C++\" e o componente \"Ferramentas "
                    + "Clang para Windows\". Ele ja traz cmake e ninja juntos \u2014 nao e preciso "
                    + "instalar nenhum dos dois a parte.";
        } else {
            cor = "#FEE685";
            rotulo = "Faltam ferramentas do projeto: " + string.Join(", ", doProjeto.ToArray());
            detalhe = "Sao do proprio ufc3Recomp, nao da Microsoft. O launcher as coloca em "
                    + Ferramentas.PastaGerida + ".";
        }

        var pincel = new SolidColorBrush((Color)ColorConverter.ConvertFromString(cor));
        var ponto = Achar("PontoFerr") as System.Windows.Shapes.Ellipse;
        if (ponto != null) ponto.Fill = pincel;
        var rot = Achar("TxtFerrRotulo") as TextBlock;
        if (rot != null) { rot.Text = rotulo; rot.Foreground = pincel; }
        var det = Achar("TxtFerrDetalhe") as TextBlock;
        if (det != null) det.Text = detalhe;
    }

    // Reflete na tela o resultado da verificacao e libera (ou nao) o botao.
    void AtualizarDisco() {
        var r = Disco.Verificar(((TextBox)Achar("TxtPasta")).Text.Trim());
        var pincel = new SolidColorBrush((Color)ColorConverter.ConvertFromString(Disco.Cor(r.Estado)));

        var ponto = Achar("PontoDisco") as System.Windows.Shapes.Ellipse;
        if (ponto != null) ponto.Fill = pincel;
        var rot = Achar("TxtDiscoRotulo") as TextBlock;
        if (rot != null) { rot.Text = r.Rotulo; rot.Foreground = pincel; }
        var det = Achar("TxtDiscoDetalhe") as TextBlock;
        if (det != null) det.Text = r.Detalhe;

        var btn = Achar("BtnInstalar") as Button;
        if (btn != null) btn.IsEnabled = r.Estado != Disco.Estado.Aguardando
                                      && r.Estado != Disco.Estado.Invalido;
    }

    void Log(string m) {
        Dispatcher.Invoke(() => {
            var t = (TextBox)Achar("TxtLog");
            t.AppendText(m + Environment.NewLine);
            t.ScrollToEnd();
        });
    }
    void Etapa(string t, double p) {
        Dispatcher.Invoke(() => {
            ((TextBlock)Achar("TxtEtapa")).Text = t;
            ((ProgressBar)Achar("BarraProgresso")).Value = p;
        });
    }

    async Task Instalar() {
        var origem = ((TextBox)Achar("TxtPasta")).Text.Trim();
        if (string.IsNullOrEmpty(origem)) {
            MessageBox.Show("Escolha a ISO ou a pasta do jogo.", "Falta a origem");
            return;
        }
        var btn = (Button)Achar("BtnInstalar");
        btn.IsEnabled = false;
        try {
            if (ModoPronto)
                await Task.Run(() => Pipeline.SomenteExtrair(origem, RaizApp, Log, Etapa));
            else
                await Task.Run(() => Pipeline.Executar(origem, Projeto, BuildDir, Bancada, Log, Etapa));
            _cfg.PastaJogo = Pipeline.PastaJogoResolvida ?? origem;
            Salvar();
            Etapa("Pronto.", 100);
            Mostrar("PainelPrincipal");
        } catch (Exception ex) {
            Log("ERRO: " + ex.Message);
            Etapa("Falhou.", 0);
        } finally { btn.IsEnabled = true; }
    }

    // A pasta do jogo pode nao estar registrada ainda (primeira execucao sem
    // passar pela configuracao). Sem ela o runtime aborta com
    // "--game_data_root was not provided", entao validamos aqui e mandamos
    // para a tela certa em vez de deixar o jogo reclamar.
    bool PastaJogoValida() {
        return !string.IsNullOrWhiteSpace(_cfg.PastaJogo)
            && File.Exists(Path.Combine(_cfg.PastaJogo, "default.xex"));
    }

    // Tenta descobrir a pasta sozinha: o mesmo default.xex que esta no projeto
    // veio de algum lugar, e o disco extraido tem DAT/ ao lado dele.
    string DetectarPastaJogo() {
        var candidatos = new List<string>();
        string perfil = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
        foreach (var raiz in new[] { Path.Combine(perfil, "Desktop"), perfil, @"C:\" }) {
            if (!Directory.Exists(raiz)) continue;
            try {
                foreach (var d in Directory.GetDirectories(raiz)) {
                    candidatos.Add(d);
                    try { candidatos.AddRange(Directory.GetDirectories(d)); } catch { }
                }
            } catch { }
        }
        foreach (var c in candidatos) {
            try {
                if (File.Exists(Path.Combine(c, "default.xex")) &&
                    Directory.Exists(Path.Combine(c, "DAT"))) return c;
            } catch { }
        }
        return null;
    }

    void Jogar() {
        UIParaConfig();
        Salvar();
        if (!File.Exists(Exe)) {
            MessageBox.Show("ufc3.exe nao encontrado. Use Reconstruir.", "Sem executavel");
            return;
        }
        if (!PastaJogoValida()) {
            string achado = DetectarPastaJogo();
            if (achado != null) {
                _cfg.PastaJogo = achado;
                Salvar();
            } else {
                MessageBox.Show(
                    "A pasta do jogo ainda nao foi registrada.\n\n" +
                    "O executavel precisa saber onde estao os arquivos do disco " +
                    "(a pasta que contem default.xex e DAT).\n\n" +
                    "Vou abrir a configuracao para voce apontar a ISO ou a pasta.",
                    "Falta a pasta do jogo", MessageBoxButton.OK, MessageBoxImage.Information);
                Mostrar("PainelSetup");
                return;
            }
        }
        var aviso = Integridade.Verificar(Exe, _hashPath);
        if (aviso != null) MessageBox.Show(aviso, "Integridade", MessageBoxButton.OK, MessageBoxImage.Information);

        // Sem argumentos de configuracao: elas vem do ufc3.toml, e a linha de
        // comando teria precedencia sobre o arquivo.
        // user_data_root e cache_root VAO NA LINHA DE COMANDO, nao no ufc3.toml.
        // Bug de ordem no SDK (ui/rex_app.cpp): os caminhos sao resolvidos a
        // partir dos cvars ANTES de LoadConfig() ler o arquivo, entao um valor
        // posto no TOML chega tarde demais e a pasta padrao prevalece --
        // silenciosamente, criando um segundo conjunto de saves.
        Directory.CreateDirectory(PastaDados);
        var psi = new ProcessStartInfo(Exe) {
            // O log e' sempre gravado: no nivel info o arquivo fica pequeno e
            // nao custa desempenho, e sem ele um problema na maquina de outra
            // pessoa nao deixa nada para investigar.
            Arguments = "--game_data_root \"" + _cfg.PastaJogo + "\""
                      + " --user_data_root \"" + PastaDados + "\""
                      + " --cache_root \"" + Path.Combine(PastaDados, "cache") + "\""
                      + " --log_file \"" + Path.Combine(PastaDados, "ufc3.log") + "\"",
            // A pasta do proprio executavel, nunca um caminho fixo: e' de la
            // que o runtime carrega rexruntime e o plugin de GPU, e um caminho
            // que nao existe faz o Process.Start lancar excecao.
            WorkingDirectory = Path.GetDirectoryName(Exe),
            UseShellExecute = false
        };
        Process.Start(psi);
        Close();
    }
}

// -----------------------------------------------------------------------------
//  Pipeline: ISO -> extracao -> codegen -> build
// -----------------------------------------------------------------------------
internal static class Pipeline {
    public static string PastaJogoResolvida;

    static int Rodar(string exe, string args, string cwd, Action<string> log, Func<string,bool> interessa) {
        var psi = new ProcessStartInfo(exe, args) {
            WorkingDirectory = cwd, UseShellExecute = false,
            RedirectStandardOutput = true, RedirectStandardError = true, CreateNoWindow = true
        };
        using (var p = Process.Start(psi)) {
            p.OutputDataReceived += (s, e) => { if (e.Data != null && (interessa == null || interessa(e.Data))) log(e.Data); };
            p.ErrorDataReceived  += (s, e) => { if (e.Data != null) log(e.Data); };
            p.BeginOutputReadLine(); p.BeginErrorReadLine();
            p.WaitForExit();
            return p.ExitCode;
        }
    }

    // Caminho do modo pronto: o executavel ja veio compilado, entao a unica
    // coisa a fazer e deixar os arquivos do jogo acessiveis e conferir que o
    // disco e mesmo UFC Undisputed 3.
    //
    // O extract-xiso viaja junto com o launcher; nada mais e necessario.
    // O extract-xiso 2.7.1 tem dois habitos que precisam ser tratados:
    //
    //   - devolve codigo de saida 1 mesmo quando termina bem;
    //   - cria uma subpasta com o nome da ISO em vez de expandir na raiz
    //     que foi pedida em -d.
    //
    // Por isso o sucesso e julgado por achar o default.xex, e nao pelo codigo
    // de saida do processo.
    static string AcharXex(string raiz, int profundidade) {
        try {
            if (File.Exists(Path.Combine(raiz, "default.xex"))) return raiz;
            if (profundidade <= 0) return null;
            foreach (var d in Directory.GetDirectories(raiz)) {
                var r = AcharXex(d, profundidade - 1);
                if (r != null) return r;
            }
        } catch { }
        return null;
    }

    public static void SomenteExtrair(string origem, string raizApp,
                                      Action<string> log, Action<string,double> etapa) {
        etapa("Verificando a origem", 5);
        string pastaJogo = origem;

        if (File.Exists(origem) && origem.EndsWith(".iso", StringComparison.OrdinalIgnoreCase)) {
            string xiso = Path.Combine(raizApp, "extract-xiso.exe");
            if (!File.Exists(xiso))
                throw new Exception("extract-xiso.exe nao esta junto do launcher; "
                                  + "aponte para uma pasta ja extraida.");

            string destino = Path.Combine(Path.GetDirectoryName(origem), "UFC3_extraido");
            string ja = AcharXex(destino, 2);
            if (ja != null) {
                log("Ja extraido em: " + ja);
                pastaJogo = ja;
            } else {
                etapa("Extraindo a ISO (sao alguns GB, demora)", 15);
                Directory.CreateDirectory(destino);
                Rodar(xiso, "-x \"" + origem + "\" -d \"" + destino + "\"", destino, log, null);
                pastaJogo = AcharXex(destino, 2);
                if (pastaJogo == null)
                    throw new Exception("a extracao terminou mas nao apareceu nenhum "
                                      + "default.xex em " + destino);
            }
        }

        etapa("Conferindo o disco", 85);
        string xex = Path.Combine(pastaJogo, "default.xex");
        if (!File.Exists(xex))
            throw new Exception("default.xex nao encontrado em " + pastaJogo);

        var r = Disco.VerificarXex(xex);
        log(r.Rotulo + " \u2014 " + r.Detalhe);
        if (r.Estado == Disco.Estado.Invalido || r.Estado == Disco.Estado.OutroJogo)
            throw new Exception(r.Rotulo);

        PastaJogoResolvida = pastaJogo;
        etapa("Pronto para jogar", 100);
    }

    public static void Executar(string origem, string projeto, string buildDir, string bancada,
                                Action<string> log, Action<string,double> etapa) {
        string tc = Path.Combine(bancada, "toolchain");
        string cmake   = Path.Combine(tc, @"cmake\bin\cmake.exe");
        string rexglue = Path.Combine(tc, @"rexglue-sdk-0.10.0\win-amd64\bin\rexglue.exe");
        string xiso    = Path.Combine(tc, @"re-tools\extract-xiso.exe");

        etapa("Verificando a origem", 5);
        string pastaJogo = origem;
        if (File.Exists(origem) && origem.EndsWith(".iso", StringComparison.OrdinalIgnoreCase)) {
            string destino = Path.Combine(Path.GetDirectoryName(origem), "UFC3_extraido");
            string ja = AcharXex(destino, 2);
            if (ja != null) {
                log("Ja extraido em: " + ja);
                destino = ja;
            } else {
                etapa("Extraindo a ISO (sao alguns GB, demora)", 10);
                Directory.CreateDirectory(destino);
                Rodar(xiso, "-x \"" + origem + "\" -d \"" + destino + "\"", destino, log, null);
                var achou = AcharXex(destino, 2);
                if (achou == null)
                    throw new Exception("a extracao terminou mas nao apareceu nenhum "
                                      + "default.xex em " + destino);
                destino = achou;
            }
            pastaJogo = destino;
        }
        PastaJogoResolvida = pastaJogo;

        string xexOrigem = Path.Combine(pastaJogo, "default.xex");
        if (!File.Exists(xexOrigem))
            throw new Exception("default.xex nao encontrado em '" + pastaJogo + "'.");
        log("default.xex localizado.");

        etapa("Preparando o projeto", 25);
        string destAssets = Path.Combine(projeto, @"assets\game");
        Directory.CreateDirectory(destAssets);
        string xexProjeto = Path.Combine(destAssets, "default.xex");
        bool copiar = true;
        if (File.Exists(xexProjeto) &&
            Integridade.Sha256(xexOrigem) == Integridade.Sha256(xexProjeto)) {
            copiar = false; log("default.xex ja esta no projeto e e identico.");
        }
        if (copiar) { File.Copy(xexOrigem, xexProjeto, true); log("default.xex copiado."); }

        etapa("Traduzindo PowerPC para C++ (cerca de um minuto)", 35);
        string manifesto = Path.Combine(projeto, "ufc3_manifest.toml");
        if (!File.Exists(manifesto)) throw new Exception("ufc3_manifest.toml nao encontrado.");
        if (Rodar(rexglue, "codegen \"" + manifesto + "\"", projeto, log,
                  l => l.Contains("phase") || l.Contains("Codegen summary") ||
                       l.Contains("Done in") || l.Contains("error") || l.Contains("Failed")) != 0)
            throw new Exception("codegen falhou.");

        etapa("Configurando o build", 55);
        if (!File.Exists(Path.Combine(buildDir, "CMakeCache.txt"))) {
            if (Rodar(cmake,
                      "--preset win-amd64-relwithdebinfo -DREXSDK_DIR=\"" + bancada + "/rexglue-sdk\"" +
                      " -DCMAKE_BUILD_TYPE=RelWithDebInfo", projeto, log,
                      l => l.Contains("Compiler") || l.Contains("ReXGlue SDK") ||
                           l.Contains("Build files") || l.Contains("Error")) != 0)
                throw new Exception("configure falhou.");
        } else log("Build ja configurado.");

        etapa("Compilando (a primeira vez leva varios minutos)", 70);
        if (Rodar(cmake, "--build \"" + buildDir + "\"", projeto, log,
                  l => l.StartsWith("[") || l.Contains("error") || l.Contains("FAILED")) != 0)
            throw new Exception("build falhou.");

        string exe = Path.Combine(buildDir, "ufc3.exe");
        if (!File.Exists(exe)) throw new Exception("build terminou mas ufc3.exe nao existe.");
        log("ufc3.exe pronto: " + Math.Round(new FileInfo(exe).Length / 1048576.0, 1) + " MB");
        etapa("Concluido", 100);
    }
}

internal static class Programa {
    [STAThread]
    static void Main() {
        var app = new Application();
        app.Run(new Janela().Win);
    }
}

}
