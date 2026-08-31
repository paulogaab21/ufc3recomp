// ufc3 - captura de falhas
//
// O ReXGlue nao instala nenhum manipulador de excecao, entao um crash duro nao
// deixa nada para investigar: o processo some e o log termina no meio de uma
// linha. Foi exatamente o que aconteceu duas vezes aqui, sem FATAL, sem evento
// do Windows e sem despejo.
//
// Este arquivo cobre os dois jeitos de morrer que importam:
//
//   excecao nao tratada   violacao de acesso e afins. O filtro roda com a
//                         pilha ainda intacta, entao o despejo tem o quadro
//                         que causou a falha.
//
//   abort()               e o que o proprio runtime chama quando encontra
//                         uma funcao nao registrada. No Windows aparece como
//                         ucrtbase.dll / 0xC0000409, que engana quem le o
//                         evento: parece corrupcao de pilha e e' so um abort.
//
// Sai um .dmp para abrir no Visual Studio e um .txt legivel com o codigo da
// excecao e o modulo+deslocamento onde ela ocorreu -- o suficiente para
// localizar a causa sem depurador na maquina de quem jogou.

#pragma once

#ifdef _WIN32

#include <windows.h>
#include <dbghelp.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace ufc3 {
namespace captura {

inline wchar_t g_pasta[MAX_PATH] = L"";
inline bool g_gravando = false;

// Primeira pasta gravavel: <pasta do exe>\diagnostico, senao
// %LOCALAPPDATA%\ufc3recomp\diagnostico.
inline const wchar_t* PastaDestino() {
  if (g_pasta[0]) return g_pasta;

  wchar_t exe[MAX_PATH];
  if (GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
    wchar_t* barra = wcsrchr(exe, L'\\');
    if (barra) {
      *barra = 0;
      swprintf(g_pasta, MAX_PATH, L"%s\\diagnostico", exe);
      if (CreateDirectoryW(g_pasta, nullptr) ||
          GetLastError() == ERROR_ALREADY_EXISTS) {
        // Confirma que da para escrever de verdade: uma pasta em Arquivos de
        // Programas aceita CreateDirectory e recusa o arquivo.
        wchar_t teste[MAX_PATH];
        swprintf(teste, MAX_PATH, L"%s\\.teste", g_pasta);
        HANDLE h = CreateFileW(teste, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE,
                               nullptr);
        if (h != INVALID_HANDLE_VALUE) {
          CloseHandle(h);
          return g_pasta;
        }
      }
    }
  }

  wchar_t* local = nullptr;
  size_t tam = 0;
  _wdupenv_s(&local, &tam, L"LOCALAPPDATA");
  if (local) {
    wchar_t base[MAX_PATH];
    swprintf(base, MAX_PATH, L"%s\\ufc3recomp", local);
    CreateDirectoryW(base, nullptr);
    swprintf(g_pasta, MAX_PATH, L"%s\\diagnostico", base);
    CreateDirectoryW(g_pasta, nullptr);
    free(local);
    return g_pasta;
  }

  wcscpy_s(g_pasta, MAX_PATH, L".");
  return g_pasta;
}

inline void CarimboDeTempo(wchar_t* saida, size_t n) {
  SYSTEMTIME t;
  GetLocalTime(&t);
  swprintf(saida, n, L"%04d%02d%02d-%02d%02d%02d", t.wYear, t.wMonth, t.wDay,
           t.wHour, t.wMinute, t.wSecond);
}

// Nome e deslocamento do modulo que contem o endereco. Sem isso o endereco
// absoluto nao serve para nada, porque o ASLR muda a base a cada execucao.
inline void DescreverEndereco(void* addr, char* saida, size_t n) {
  HMODULE mod = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         static_cast<LPCSTR>(addr), &mod) && mod) {
    char caminho[MAX_PATH] = "";
    GetModuleFileNameA(mod, caminho, MAX_PATH);
    const char* nome = strrchr(caminho, '\\');
    nome = nome ? nome + 1 : caminho;
    unsigned long long base = reinterpret_cast<unsigned long long>(mod);
    unsigned long long a = reinterpret_cast<unsigned long long>(addr);
    snprintf(saida, n, "%s+0x%llX", nome, a - base);
  } else {
    snprintf(saida, n, "0x%p (fora de qualquer modulo carregado)", addr);
  }
}

inline const char* NomeDaExcecao(DWORD code) {
  switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "violacao de acesso";
    case EXCEPTION_STACK_OVERFLOW:        return "estouro de pilha";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "instrucao ilegal";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "divisao inteira por zero";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "divisao flutuante por zero";
    case EXCEPTION_PRIV_INSTRUCTION:      return "instrucao privilegiada";
    case EXCEPTION_IN_PAGE_ERROR:         return "erro de pagina";
    case 0xC0000409:                      return "abort() / __fastfail";
    case 0xE06D7363:                      return "excecao de C++ nao capturada";
    default:                              return "desconhecida";
  }
}

inline void Gravar(EXCEPTION_POINTERS* ep, const char* origem) {
  if (g_gravando) return;   // uma falha dentro do proprio manipulador
  g_gravando = true;

  const wchar_t* pasta = PastaDestino();
  wchar_t carimbo[32];
  CarimboDeTempo(carimbo, 32);
  // O pid entra no nome porque o carimbo tem resolucao de um segundo: duas
  // falhas no mesmo segundo sobrescreviam uma a outra.
  const DWORD pid = GetCurrentProcessId();

  // --- despejo binario, para abrir no Visual Studio ---
  wchar_t dmp[MAX_PATH];
  swprintf(dmp, MAX_PATH, L"%s\\ufc3-%s-%lu.dmp", pasta, carimbo, pid);
  HANDLE h = CreateFileW(dmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    // WithIndirectlyReferencedMemory traz o que os registradores apontam, que
    // e o que permite ler o estado do guest sem um despejo completo de GB.
    const MINIDUMP_TYPE tipo = static_cast<MINIDUMP_TYPE>(
        MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithDataSegs |
        MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), h, tipo,
                      ep ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(h);
  }

  // --- relatorio legivel, que e o que a pessoa consegue mandar por mensagem ---
  wchar_t txt[MAX_PATH];
  swprintf(txt, MAX_PATH, L"%s\\ufc3-%s-%lu.txt", pasta, carimbo, pid);
  FILE* f = nullptr;
  _wfopen_s(&f, txt, L"w");
  if (f) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(f, "ufc3Recomp -- relatorio de falha\n");
    fprintf(f, "================================\n\n");
    fprintf(f, "Quando : %04d-%02d-%02d %02d:%02d:%02d\n",
            t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    fprintf(f, "Origem : %s\n", origem);
    fprintf(f, "Thread : %lu\n\n", GetCurrentThreadId());

    if (ep && ep->ExceptionRecord) {
      DWORD code = ep->ExceptionRecord->ExceptionCode;
      void* addr = ep->ExceptionRecord->ExceptionAddress;
      char onde[512];
      DescreverEndereco(addr, onde, sizeof onde);
      fprintf(f, "Excecao: 0x%08lX (%s)\n", code, NomeDaExcecao(code));
      fprintf(f, "Local  : %s\n", onde);

      if (code == EXCEPTION_ACCESS_VIOLATION &&
          ep->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR* p = ep->ExceptionRecord->ExceptionInformation;
        const char* acao = p[0] == 0 ? "leitura" : (p[0] == 1 ? "escrita" : "execucao");
        fprintf(f, "Acesso : %s de 0x%016llX\n", acao,
                static_cast<unsigned long long>(p[1]));
        if (p[1] < 0x10000)
          fprintf(f, "         (endereco baixo -- provavelmente ponteiro nulo)\n");
      }
    } else {
      fprintf(f, "Sem registro de excecao: o processo terminou por chamada\n"
                 "explicita, nao por falha de hardware.\n");
    }

    fprintf(f, "\nDespejo: %ls\n", dmp);
    fprintf(f, "\nO que fazer com isto\n");
    fprintf(f, "--------------------\n");
    fprintf(f, "Mande os dois arquivos junto com o ufc3.log que esta na pasta\n");
    fprintf(f, "userdata, em github.com/paulogaab21/ufc3recomp/issues.\n");
    fprintf(f, "\nSe a linha 'Excecao' disser abort(), a causa costuma estar na\n");
    fprintf(f, "ultima linha do log: uma funcao do jogo que nao foi traduzida.\n");
    fclose(f);
  }
}

inline LONG WINAPI FiltroDeExcecao(EXCEPTION_POINTERS* ep) {
  Gravar(ep, "excecao nao tratada");
  return EXCEPTION_EXECUTE_HANDLER;   // encerra depois de gravar
}

// Num abort() nao existe EXCEPTION_POINTERS, e sem ele o despejo nao marca
// qual thread causou a parada -- quem abre no depurador cai numa thread
// qualquer. Capturamos o contexto atual e montamos um registro sintetico, para
// que o despejo aponte para o ponto exato onde o runtime desistiu.
inline void GravarComContextoAtual(const char* origem, DWORD codigo) {
  CONTEXT ctx;
  RtlCaptureContext(&ctx);

  EXCEPTION_RECORD er{};
  er.ExceptionCode = codigo;
  er.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
  er.ExceptionAddress = reinterpret_cast<void*>(
#if defined(_M_X64) || defined(__x86_64__)
      ctx.Rip
#else
      ctx.Pc
#endif
  );

  EXCEPTION_POINTERS ep{};
  ep.ExceptionRecord = &er;
  ep.ContextRecord = &ctx;
  Gravar(&ep, origem);
}

inline void AoAbortar(int) {
  GravarComContextoAtual("abort() -- o runtime encerrou de proposito", 0xC0000409);
  _exit(3);
}

// Instalar cedo, logo depois que o log sobe.
inline void Instalar() {
  SetUnhandledExceptionFilter(FiltroDeExcecao);
  signal(SIGABRT, AoAbortar);
  signal(SIGSEGV, [](int) {
    GravarComContextoAtual("SIGSEGV", EXCEPTION_ACCESS_VIOLATION);
    _exit(3);
  });
  // Sem isto o CRT abre a caixa "o programa parou de funcionar" e engole o
  // nosso manipulador.
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}

}  // namespace captura
}  // namespace ufc3

#else
namespace ufc3::captura { inline void Instalar() {} }
#endif  // _WIN32
