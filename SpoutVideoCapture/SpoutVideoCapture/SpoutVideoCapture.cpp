#include "pch.hpp"

#include <windows.h>

#include <cstdio>
#include <exception>
#include <memory>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include "../../SpoutSDK/Source/SpoutSender.h"


#pragma comment(lib, "Opengl32.lib")


namespace
{
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
        on_exit();
      }
    }

    bool execute_on_exit;

  private:
    TOnExit on_exit;
  };

  template<typename TOnExit>
  auto on_exit(TOnExit && on_exit)
  {
    return on_exit_guard<std::decay_t<TOnExit>> (std::forward<TOnExit> (on_exit));
  }


  HWND  hwnd          ;
  HDC   hdc           ;
  HGLRC hglrc         ;
  HGLRC shared_hglrc  ;

  bool init_open_gl ()
  {
    hwnd  = CreateWindowA ("BUTTON", "VDJ Sender", WS_OVERLAPPEDWINDOW | CS_OWNDC, 0, 0, 32, 32, NULL, NULL, NULL, NULL);
	  hdc   = GetDC (hwnd);

	  PIXELFORMATDESCRIPTOR pfd {};
	  pfd.nSize         = sizeof (pfd);
	  pfd.nVersion      = 1;
	  pfd.dwFlags       = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	  pfd.iPixelType    = PFD_TYPE_RGBA;
	  pfd.cColorBits    = 32;
	  pfd.cDepthBits    = 24;
	  pfd.cStencilBits  = 8;
	  pfd.iLayerType    = PFD_MAIN_PLANE;

	  auto format = ChoosePixelFormat (hdc, &pfd);
	  SetPixelFormat (hdc, format, &pfd);
	  hglrc = wglCreateContext (hdc);
	  wglMakeCurrent (hdc, hglrc);

//    shared_hglrc = wglCreateContext (hdc);
//	  wglShareLists (hsrc, hrc);


	  return true;

  }


}

int main()
{
  try
  {
    HRESULT hr = CoInitialize (0);
    if (FAILED (hr))
    {
      throw new std::runtime_error ("Hello");
    }

    init_open_gl ();


    SpoutSender sender;

    auto width = 640;
    auto height = 480;

    auto sender_created = sender.CreateSender ("Spout Video Capture", width, height);
    if (!sender_created)
    {
      throw new std::runtime_error ("Failed to create spout sender");
    }

    std::vector<unsigned char> image;
    image.resize (width*height*4);

    std::mt19937 random;
    random.seed (19740531U);

    auto cont = true;
    auto i = 0;

    while (cont)
    {
      for(auto i = 0U; i < image.size(); ++i) 
      {
        image[i] = random () >> 24;
      }
      ++i;
      if ((i % 60) == 0)
      {
        std::printf ("Sending frame #%d\n", i);
      }
      sender.SendImage(&image.front (), width, height);
      Sleep(20);
    }


    auto on_exit_co_uninitialize = on_exit ([] { CoUninitialize (); });

    std::printf("hello \n");
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

