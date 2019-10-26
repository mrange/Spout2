#include "pch.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <exception>
#include <future>
#include <memory>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "../../SpoutSDK/Source/SpoutSender.h"


#pragma comment(lib, "Opengl32.lib")

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define CHECK(msg, v) check(__FILE__ "(" TOSTRING (__LINE__) ") Aborted when trying to: " msg, v)

#define CHECK_HR(msg, hr) check_hr(__FILE__ "(" TOSTRING (__LINE__) ") Aborted when trying to: " msg, hr)

namespace
{
  struct unit {};
  constexpr unit unit_value;

  template<typename TOnExit>
  struct on_exit_guard 
  {
    on_exit_guard             ()                      = delete;
    on_exit_guard             (on_exit_guard const &) = delete;
    on_exit_guard& operator=  (on_exit_guard &&     ) = delete;
    on_exit_guard& operator=  (on_exit_guard const &) = delete;

    on_exit_guard (TOnExit on_exit) 
      : execute_on_exit (true)
      , on_exit         (std::move (on_exit))
    {
    }

    on_exit_guard (on_exit_guard && oeg)
      : execute_on_exit (oeg.execute_on_exit)
      , on_exit         (std::move (oeg.on_exit))
    {
      oeg.execute_on_exit = false;
    }

    ~on_exit_guard () noexcept
    {
      if (execute_on_exit)
      {
        execute_on_exit = false;
        on_exit ();
      }
    }

    bool execute_on_exit;

  private:
    TOnExit on_exit;
  };

  template<typename TOnExit>
  auto on_exit (TOnExit && on_exit)
  {
    return on_exit_guard<std::decay_t<TOnExit>> (std::forward<TOnExit> (on_exit));
  }

  template<typename T>
  T check (const char* abort_with_message, T && v)
  {
    if (!v)
    {
      throw std::runtime_error (abort_with_message);
    }

    return std::forward<T> (v);
  }

  HRESULT check_hr (const char* abort_with_message, HRESULT hr)
  {
    if (FAILED (hr))
    {
      throw std::runtime_error (abort_with_message);
    }

    return hr;
  }

  char const app_name [] = "Spout Video Capture";

}

int main ()
{
  try
  {
    auto done = std::async ([] 
      {
        // Try to read a char from stdin (blocking)
        auto ignore = std::getchar ();
        return unit_value; 
      });

    std::printf ("Initializing video capture...\n");

    CHECK_HR ("Initialize COM Runtime", CoInitialize (0));
    auto on_exit_co_uninitialize = on_exit ([] { CoUninitialize (); });

    // An invisible window used to initialze Open GL with
    auto hwnd                   = CHECK ("Create invisible window for Open GL", CreateWindowA ("BUTTON", app_name, WS_OVERLAPPEDWINDOW | CS_OWNDC, 0, 0, 32, 32, nullptr, nullptr, nullptr, nullptr));
    auto on_exit_destroy_window = on_exit ([hwnd] { DestroyWindow (hwnd); });

    auto hdc                = CHECK ("Get device context for Open GL", GetDC (hwnd));
    auto on_exit_release_dc = on_exit ([hwnd, hdc] { ReleaseDC (hwnd, hdc); });

    PIXELFORMATDESCRIPTOR pfd {};
    pfd.nSize         = sizeof (pfd);
    pfd.nVersion      = 1;
    pfd.dwFlags       = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType    = PFD_TYPE_RGBA;
    pfd.cColorBits    = 32;
    pfd.cDepthBits    = 24;
    pfd.cStencilBits  = 8;
    pfd.iLayerType    = PFD_MAIN_PLANE;

    auto format = CHECK ("Choose pixel format for device context", ChoosePixelFormat (hdc, &pfd));
    CHECK ("Set pixel format for device context", SetPixelFormat (hdc, format, &pfd));

    auto hglrc                      = CHECK ("Create OpenGL context", wglCreateContext (hdc));
    auto on_exit_delete_gl_context  = on_exit ([hglrc] { wglDeleteContext (hglrc); });

    CHECK ("Make OpenGL context current", wglMakeCurrent (hdc, hglrc));
    auto on_exit_unselect_gl_context  = on_exit ([hdc] { wglMakeCurrent (hdc, nullptr); });

    SpoutSender sender;

    auto width  = 640;
    auto height = 480;

    CHECK ("Create Spout sender", sender.CreateSender (app_name, width, height));    
    auto on_exit_release_sender = on_exit ([&sender] { sender.ReleaseSender (200); });

    std::vector<unsigned char> image;
    image.resize (width*height*4);

    std::mt19937 random;
    random.seed (19740531U);

    auto cont = true;
    auto i = 0;

    std::printf ("Initializing video capture done, sending frames as: %s...\n", app_name);

    std::printf ("Hit enter to exit");

    while (done.wait_for (std::chrono::milliseconds (20)) == future_status::timeout)
    {
      for (auto i = 0U; i < image.size (); ++i) 
      {
        image[i] = random () >> 24;
      }
      ++i;
      if ((i % 60) == 0)
      {
        std::printf ("Sending frame #%d\n", i);
      }
      sender.SendImage (&image.front (), width, height);
    }

    std::printf ("Ok, we are done, exiting...");

    return 0;
  }
  catch (std::exception const & e)
  {
    std::printf ("Problem detected - %s. Exiting.", e.what ());
    return 998;
  }
  catch (...)
  {
    std::printf ("Unknown problem detected. Exiting.");
    return 999;
  }
}

