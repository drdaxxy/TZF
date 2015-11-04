/**
 * This file is part of Tales of Zestiria "Fix".
 *
 * Tales of Zestiria "Fix" is free software : you can redistribute it
 * and/or modify it under the terms of the GNU General Public License
 * as published by The Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * Tales of Zestiria "Fix" is distributed in the hope that it will be
 * useful,
 *
 * But WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Tales of Zestiria "Fix".
 *
 *   If not, see <http://www.gnu.org/licenses/>.
 *
**/
#include <string>

#include "hook.h"
#include "log.h"
#include "command.h"
#include "sound.h"
#include "framerate.h"
#include "config.h"

#include <mmsystem.h>
#pragma comment (lib, "winmm.lib")

#include <comdef.h>

struct window_t {
  DWORD proc_id;
  HWND  root;
};

BOOL
CALLBACK
TZF_EnumWindows (HWND hWnd, LPARAM lParam)
{
  window_t& win = *(window_t*)lParam;

  DWORD proc_id = 0;

  GetWindowThreadProcessId (hWnd, &proc_id);

  if (win.proc_id != proc_id) {
    if (GetWindow (hWnd, GW_OWNER) != (HWND)nullptr ||
        GetWindowTextLength (hWnd) < 30             ||
     (! IsWindowVisible     (hWnd)))
      return TRUE;
  }

  win.root = hWnd;
  return FALSE;
}

HWND
TZF_FindRootWindow (DWORD proc_id)
{
  window_t win;

  win.proc_id  = proc_id;
  win.root     = 0;

  EnumWindows (TZF_EnumWindows, (LPARAM)&win);

  return win.root;
}

class TZF_KeyboardHooker
{
private:
  HANDLE                     hMsgPump;
  HHOOK                      hHook;
  static TZF_KeyboardHooker* pKeyboardHook;

  static char                text [16384];

  static BYTE keys_ [256];
  static bool visible;

  static bool command_issued;
  static std::string result_str;

protected:
  TZF_KeyboardHooker (void) { }

public:
  static TZF_KeyboardHooker* getInstance (void)
  {
    if (pKeyboardHook == NULL)
      pKeyboardHook = new TZF_KeyboardHooker ();

    return pKeyboardHook;
  }

  void Start (void)
  {
    hMsgPump =
      CreateThread ( NULL,
                       NULL,
                         TZF_KeyboardHooker::MessagePump,
                           &hHook,
                             NULL,
                               NULL );
  }

  void End (void)
  {
    TerminateThread     (hMsgPump, 0);
    UnhookWindowsHookEx (hHook);
  }

  HANDLE GetThread (void)
  {
    return hMsgPump;
  }

  static DWORD
  WINAPI
  MessagePump (LPVOID hook_ptr)
  {
    ZeroMemory (text, 16384);

    text [0] = '>';

    extern    HMODULE hDLLMod;

    HWND  hWndForeground;
    DWORD dwThreadId;

    int hits = 0;

    DWORD dwTime = timeGetTime ();

    while (true) {
      // Spin until either the soundfix is initialized or 15 seconds elapse;
      //   this delays initialization of the hook and improves compatibility.
      if ((! tzf::SoundFix::wasapi_init) &&
          ((timeGetTime () - dwTime) < 15000)) {
        Sleep (83);
        continue;
      }

      hWndForeground = GetForegroundWindow ();

      if ((! hWndForeground) ||
             hWndForeground != TZF_FindRootWindow (GetCurrentProcessId ())) {
        Sleep (83);
        continue;
      }

      dwThreadId = GetWindowThreadProcessId (hWndForeground, nullptr);

      break;
    }

    dll_log.Log ( L"  # Found window in %03.01f seconds, "
                     L"installing keyboard hook...",
                   (float)(timeGetTime () - dwTime) / 1000.0f );

    typedef BOOL (__stdcall *BMF_DrawExternalOSD_t)(std::string app_name, std::string text);

    static HMODULE               hMod =
      LoadLibrary (L"d3d9.dll");
    static BMF_DrawExternalOSD_t BMF_DrawExternalOSD
      =
      (BMF_DrawExternalOSD_t)GetProcAddress (hMod, "BMF_DrawExternalOSD");

    dwTime = timeGetTime ();
    hits   = 1;

    while (! (*(HHOOK *)hook_ptr = SetWindowsHookEx ( WH_KEYBOARD,
                                              KeyboardProc,
                                                hDLLMod,
                                                  dwThreadId ))) {
      _com_error err (HRESULT_FROM_WIN32 (GetLastError ()));

      dll_log.Log ( L"  @ SetWindowsHookEx failed: 0x%04X (%s)",
                    err.WCode (), err.ErrorMessage () );

      ++hits;

      if (hits >= 5) {
        dll_log.Log ( L"  * Failed to install keyboard hook after %lu tries... "
          L"bailing out!",
          hits );
        return 0;
      }

      Sleep (1);
    }

    dll_log.Log ( L"  * Installed keyboard hook for command console... "
                        L"%lu %s (%lu ms!)",
                  hits,
                    hits > 1 ? L"tries" : L"try",
                      timeGetTime () - dwTime );

    DWORD last_time = timeGetTime ();
    bool  carret    = true;

    //193 - 199

    while (true)
    {
      std::string output;

      if (config.framerate.stutter_fix && (! tzf::FrameRateFix::fullscreen)) {
        output += "<Run Game in Fullscreen Mode or Disable Stutter Fix!>\n";
      }

      if (visible) {
        output += text;

        // Blink the Carret
        if (timeGetTime () - last_time > 333) {
          carret = ! carret;

          last_time = timeGetTime ();
        }

        if (carret)
          output += "-";

        // Show Command Results
        if (command_issued) {
          output += "\n";
          output += result_str;
        }
      }

      BMF_DrawExternalOSD ("ToZ Fix", output.c_str ());

      Sleep (16);
    }

    return 0;
  }

  static LRESULT
  CALLBACK
  KeyboardProc (int nCode, WPARAM wParam, LPARAM lParam)
  {
    typedef BOOL (__stdcall *BMF_DrawExternalOSD_t)(std::string app_name, std::string text);

    static HMODULE               hMod =
      LoadLibrary (L"d3d9.dll");
    static BMF_DrawExternalOSD_t BMF_DrawExternalOSD
                                      =
      (BMF_DrawExternalOSD_t)GetProcAddress (hMod, "BMF_DrawExternalOSD");

    if (nCode >= 0) {
      if (true) {
        DWORD   vkCode   = LOWORD (wParam);
        DWORD   scanCode = HIWORD (lParam) & 0x7F;
        bool    repeated = LOWORD (lParam);
        bool    keyDown  = ! (lParam & 0x80000000);

        if (visible && vkCode == VK_BACK) {
          if (keyDown) {
            int len = strlen (text);
            len--;
            if (len < 1)
              len = 1;
            text [len] = '\0';
          }
        } else if ((vkCode == VK_SHIFT || vkCode == VK_LSHIFT || vkCode == VK_RSHIFT)) {
          if (keyDown)
            keys_ [VK_SHIFT] = 0x81;
          else
            keys_ [VK_SHIFT] = 0x00;
        }
        else if ((!repeated) && vkCode == VK_CAPITAL) {
          if (keyDown) {
            if (keys_ [VK_CAPITAL] == 0x00)
              keys_ [VK_CAPITAL] = 0x81;
            else
              keys_ [VK_CAPITAL] = 0x00;
          }
        }
        else if ((vkCode == VK_CONTROL || vkCode == VK_LCONTROL || vkCode == VK_RCONTROL)) {
          if (keyDown)
            keys_ [VK_CONTROL] = 0x81;
          else
            keys_ [VK_CONTROL] = 0x00;
        }
        else if (visible && vkCode == VK_RETURN) {
          if (keyDown && LOWORD (lParam) < 2) {
            int len = strlen (text+1);
            // Don't process empty or pure whitespace command lines
            if (len > 0 && strspn (text+1, " ") != len) {
              eTB_CommandResult result = command.ProcessCommandLine (text+1);

              if (result.getStatus ()) {
                text [1] = '\0';
                command_issued = true;
              }
              else {
                command_issued = false;
              }

              result_str     = result.getWord () + std::string (" ")   +
                               result.getArgs () + std::string (":  ") +
                               result.getResult ();
            }
          }
        }
        else if (keyDown) {
          bool new_press = keys_ [vkCode] != 0x81;

          keys_ [vkCode] = 0x81;

          if (keys_ [VK_CONTROL] && keys_ [VK_SHIFT] && keys_ [VK_TAB] && new_press)
            visible = ! visible;

          if (visible) {
            char key_str [2];
            key_str [1] = '\0';

            if (1 == ToAsciiEx ( vkCode,
                                 scanCode,
                                 keys_,
                                (LPWORD)key_str,
                                 0,
                                 GetKeyboardLayout (0) )) {
              strncat (text, key_str, 1);
              command_issued = false;
            }
          }
        } else if ((! keyDown)) {
          keys_ [vkCode] = 0x00;
        }

        if (visible)
          return 1;
      }
    }

    return CallNextHookEx (TZF_KeyboardHooker::getInstance ()->hHook, nCode, wParam, lParam);
  };
};



typedef DECLSPEC_IMPORT HMODULE (WINAPI *LoadLibraryA_t)(LPCSTR  lpFileName);
typedef DECLSPEC_IMPORT HMODULE (WINAPI *LoadLibraryW_t)(LPCWSTR lpFileName);

LoadLibraryA_t LoadLibraryA_Original = nullptr;
LoadLibraryW_t LoadLibraryW_Original = nullptr;

HMODULE
WINAPI
LoadLibraryA_Detour (LPCSTR lpFileName)
{
  if (lpFileName == nullptr)
    return NULL;

  HMODULE hMod = LoadLibraryA_Original (lpFileName);

  if (strstr (lpFileName, "steam_api") ||
      strstr (lpFileName, "SteamworksNative")) {
    //TZF::SteamAPI::Init (false);
  }

  return hMod;
}

HMODULE
WINAPI
LoadLibraryW_Detour (LPCWSTR lpFileName)
{
  if (lpFileName == nullptr)
    return NULL;

  HMODULE hMod = LoadLibraryW_Original (lpFileName);

  if (wcsstr (lpFileName, L"steam_api") ||
      wcsstr (lpFileName, L"SteamworksNative")) {
    //TZF::SteamAPI::Init (false);
  }

  return hMod;
}





MH_STATUS
WINAPI
TZF_CreateFuncHook ( LPCWSTR pwszFuncName,
                     LPVOID  pTarget,
                     LPVOID  pDetour,
                     LPVOID *ppOriginal )
{
  MH_STATUS status =
    MH_CreateHook ( pTarget,
                    pDetour,
                    ppOriginal );

  if (status != MH_OK) {
    dll_log.Log ( L" [ MinHook ] Failed to Install Hook for '%s' "
                  L"[Address: %04Xh]!  (Status: \"%hs\")",
                    pwszFuncName,
                      pTarget,
                        MH_StatusToString (status) );
  }

  return status;
}

MH_STATUS
WINAPI
TZF_CreateDLLHook ( LPCWSTR pwszModule, LPCSTR  pszProcName,
                    LPVOID  pDetour,    LPVOID *ppOriginal,
                    LPVOID *ppFuncAddr )
{
#if 1
  HMODULE hMod = GetModuleHandle (pwszModule);

  if (hMod == NULL) {
    if (LoadLibraryW_Original != nullptr) {
      hMod = LoadLibraryW_Original (pwszModule);
    } else {
      hMod = LoadLibraryW (pwszModule);
    }
  }

  LPVOID pFuncAddr =
    GetProcAddress (hMod, pszProcName);

  MH_STATUS status =
    MH_CreateHook ( pFuncAddr,
      pDetour,
      ppOriginal );
#else
  MH_STATUS status =
    MH_CreateHookApi ( pwszModule,
      pszProcName,
      pDetour,
      ppOriginal );
#endif

  if (status != MH_OK) {
    dll_log.Log ( L" [ MinHook ] Failed to Install Hook for: '%hs' in '%s'! "
                  L"(Status: \"%hs\")",
                    pszProcName,
                      pwszModule,
                        MH_StatusToString (status) );
  }

  if (ppFuncAddr != nullptr)
    *ppFuncAddr = pFuncAddr;

  return status;
}

MH_STATUS
WINAPI
TZF_EnableHook (LPVOID pTarget)
{
  MH_STATUS status =
    MH_EnableHook (pTarget);

  if (status != MH_OK)
  {
    if (pTarget != MH_ALL_HOOKS) {
      dll_log.Log( L" [ MinHook ] Failed to Enable Hook with Address: %04Xh!"
                   L" (Status: \"%hs\")",
                     pTarget,
                       MH_StatusToString (status) );
    } else {
      dll_log.Log ( L" [ MinHook ] Failed to Enable All Hooks! "
                    L"(Status: \"%hs\")",
                      MH_StatusToString (status) );
    }
  }

  return status;
}

MH_STATUS
WINAPI
TZF_DisableHook (LPVOID pTarget)
{
  MH_STATUS status =
    MH_DisableHook (pTarget);

  if (status != MH_OK)
  {
    if (pTarget != MH_ALL_HOOKS) {
      dll_log.Log ( L" [ MinHook ] Failed to Disable Hook with Address: %04Xh!"
                    L" (Status: \"%hs\")",
                      pTarget,
                        MH_StatusToString (status) );
    } else {
      dll_log.Log ( L" [ MinHook ] Failed to Disable All Hooks! "
                    L"(Status: \"%hs\")",
                      MH_StatusToString (status) );
    }
  }

  return status;
}

MH_STATUS
WINAPI
TZF_RemoveHook (LPVOID pTarget)
{
  MH_STATUS status =
    MH_RemoveHook (pTarget);

  if (status != MH_OK)
  {
    dll_log.Log ( L" [ MinHook ] Failed to Remove Hook with Address: %04Xh! "
                  L"(Status: \"%hs\")",
                    pTarget,
                      MH_StatusToString (status) );
  }

  return status;
}

MH_STATUS
WINAPI
TZF_Init_MinHook (void)
{
  MH_STATUS status;

  if ((status = MH_Initialize ()) != MH_OK)
  {
    dll_log.Log ( L" [ MinHook ] Failed to Initialize MinHook Library! "
                  L"(Status: \"%hs\")",
                    MH_StatusToString (status) );
  }

#if 0
  //
  // Hook LoadLibrary so that we can watch for things like steam_api*.dll
  //
  TZF_CreateDLLHook ( L"kernel32.dll",
                       "LoadLibraryA",
                      LoadLibraryA_Detour,
           (LPVOID *)&LoadLibraryA_Original );

  TZF_CreateDLLHook ( L"kernel32.dll",
                       "LoadLibraryW",
                      LoadLibraryW_Detour,
           (LPVOID *)&LoadLibraryW_Original );
#endif

  //TZF_EnableHook (MH_ALL_HOOKS);

  TZF_KeyboardHooker* pHook = TZF_KeyboardHooker::getInstance ();
  pHook->Start ();

  return status;
}

MH_STATUS
WINAPI
TZF_UnInit_MinHook (void)
{
  MH_STATUS status;

  if ((status = MH_Uninitialize ()) != MH_OK) {
    dll_log.Log ( L" [ MinHook ] Failed to Uninitialize MinHook Library! "
                  L"(Status: \"%hs\")",
                    MH_StatusToString (status) );
  }

  TZF_KeyboardHooker* pHook = TZF_KeyboardHooker::getInstance ();
  pHook->End ();

  return status;
}


TZF_KeyboardHooker* TZF_KeyboardHooker::pKeyboardHook;
char                TZF_KeyboardHooker::text [16384];

BYTE TZF_KeyboardHooker::keys_ [256] = { 0 };
bool TZF_KeyboardHooker::visible     = false;

bool TZF_KeyboardHooker::command_issued = false;
std::string TZF_KeyboardHooker::result_str;