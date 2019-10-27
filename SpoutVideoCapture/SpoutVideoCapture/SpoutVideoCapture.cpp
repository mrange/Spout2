#include "pch.hpp"

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

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


#pragma comment(lib, "Mf.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "Mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

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

  void release(IUnknown * p)
  {
    if (p)
    {
      p->Release ();
    }
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

  std::wstring to_string (GUID const & v)
  {
    wchar_t s[64];
    StringFromGUID2 (v, s, 64);
    return s;
  }

  char const app_name [] = "Spout Video Capture";

}

int main ()
{
  try
  {
    std::printf ("Initializing video capture...\n");

    CHECK_HR ("Initialize COM Runtime", CoInitialize (0));
    auto on_exit_co_uninitialize = on_exit ([] { CoUninitialize (); });

    CHECK_HR ("Initialize Media Foundation", MFStartup (MF_VERSION));
    auto on_exit_mf_shutdown = on_exit ([] { MFShutdown (); });

    IMFAttributes * mf_attributes = nullptr;

    CHECK_HR ("Getting media foundation attributes", MFCreateAttributes(&mf_attributes, 1));
    auto on_exit_release_attributes = on_exit ([mf_attributes] { release (mf_attributes); });

    CHECK_HR ("Setting media source query to video", mf_attributes->SetGUID (MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));

    IMFActivate ** mf_devices = nullptr;
    UINT32 mf_device_count = 0;
    CHECK_HR ("Enumerating video sources", MFEnumDeviceSources (mf_attributes, &mf_devices, &mf_device_count));
    auto on_exit_release_devices = on_exit ([mf_devices, mf_device_count] 
    {
      for (auto i = 0U; i < mf_device_count; ++i)
      {
        release (mf_devices[i]);
      }
      CoTaskMemFree (mf_devices);
    });

    if (mf_device_count == 0)
    {
      throw std::runtime_error ("No video sources found");
    }

    std::printf ("Found %d video sources\n", mf_device_count);
    for (auto i = 0U; i < mf_device_count; ++i)
    {
      auto mf_device = mf_devices[i];
      UINT32 mf_device_name_length = 0;
      std::vector<WCHAR> mf_device_name;

      CHECK_HR ("Get video source name length", mf_device->GetStringLength (
          MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME
        , &mf_device_name_length
        ));

      mf_device_name.resize (mf_device_name_length + 1);

      CHECK_HR ("Get video source name", mf_device->GetString (
          MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME
        , &mf_device_name.front ()
        , mf_device_name.size ()
        , nullptr
        ));
      mf_device_name.back () = 0;
      std::printf ("  %S\n", &mf_device_name.front ());
    }

    std::printf ("Selecting the first video source\n");

    auto mf_device = mf_devices[0];

    IMFMediaSource * mf_source = nullptr;
    CHECK_HR ("Activate video source", mf_device->ActivateObject (IID_PPV_ARGS (&mf_source)));
    auto on_exit_release_source = on_exit ([mf_source] { release (mf_source); });

    IMFSourceReader * mf_source_reader = nullptr;
    CHECK_HR ("Create video source reader", MFCreateSourceReaderFromMediaSource (mf_source, mf_attributes, &mf_source_reader));
    auto on_exit_release_source_reader = on_exit ([mf_source_reader] { release (mf_source_reader); });

    CHECK_HR ("Deselect all streams" ,mf_source_reader->SetStreamSelection (MF_SOURCE_READER_ALL_STREAMS, FALSE));
    CHECK_HR ("Select first video stream" ,mf_source_reader->SetStreamSelection (MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE));

    IMFMediaType * mf_media_type = 0;
//    CHECK_HR ("Create media type", MFCreateMediaType(&mf_media_type));
    CHECK_HR ("Get current media type", mf_source_reader->GetCurrentMediaType (MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mf_media_type));
    auto on_exit_release_media_type = on_exit ([mf_media_type] { release (mf_media_type); });

    DWORD index = 0;

    for (;;) 
    {
      IMFMediaType * mf_native_media_type = 0;
      auto hr = mf_source_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, index, &mf_native_media_type);
      if (SUCCEEDED (hr))
      {
        auto on_exit_release_media_type = on_exit ([mf_native_media_type] { release (mf_native_media_type); });
        GUID major_type {};
        GUID minor_type {};

        CHECK_HR ("Get major media type", mf_native_media_type->GetGUID (MF_MT_MAJOR_TYPE, &major_type));
        CHECK_HR ("Get minor media type", mf_native_media_type->GetGUID (MF_MT_SUBTYPE, &minor_type));
        printf ("%S\n", to_string (major_type).c_str ());
        printf ("%S\n", to_string (minor_type).c_str ());

        char x[16]="";
        memcpy(x, &minor_type.Data1, 4);
        printf ("%s\n", x);
      }
      else
      {
        break;
      }

      ++index;
    }


    CHECK_HR ("Set major media type", mf_media_type->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Video));
    CHECK_HR ("Set minor media type", mf_media_type->SetGUID (MF_MT_SUBTYPE, MFVideoFormat_ARGB32));
    CHECK_HR ("Set media type", mf_source_reader->SetCurrentMediaType (MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, mf_media_type));

    CHECK_HR ("Select first video stream" ,mf_source_reader->SetStreamSelection (MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE));

    UINT32 frame_width   = 0;
    UINT32 frame_height  = 0;
    CHECK_HR ("Read video frame size", MFGetAttributeSize (mf_media_type, MF_MT_FRAME_SIZE, &frame_width, &frame_height));

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

    CHECK ("Create Spout sender", sender.CreateSender (app_name, frame_width, frame_height));    
    auto on_exit_release_sender = on_exit ([&sender] { sender.ReleaseSender (200); });

    auto cont = true;
    auto i = 0;

    std::printf ("Initializing video capture done, sending frames as: %s...\n", app_name);

    std::printf ("Hit enter to exit\n");

    auto done = std::async ([] 
      {
        // Try to read a char from stdin (blocking)
        auto ignore = std::getchar ();
        return unit_value; 
      });

    while (done.wait_for (std::chrono::milliseconds (20)) == future_status::timeout)
    {
      ++i;
      DWORD     stream_index      = 0;
      DWORD     stream_flags      = 0;
      LONGLONG  stream_timestamp  = 0;
      IMFSample * mf_sample       = nullptr;
      CHECK_HR ("Read video sample", mf_source_reader->ReadSample (
          MF_SOURCE_READER_FIRST_VIDEO_STREAM
        , 0
        , &stream_index
        , &stream_flags
        , &stream_timestamp
        , &mf_sample));
      auto on_exit_release_sample = on_exit ([mf_sample] { release (mf_sample); });

      if (mf_sample)
      {
        DWORD buffer_count = 0;
        CHECK_HR ("Get video buffer count", mf_sample->GetBufferCount (&buffer_count));
        if (buffer_count > 0)
        {
          IMFMediaBuffer * mf_buffer = nullptr;
          CHECK_HR ("Get video buffer", mf_sample->GetBufferByIndex (0, &mf_buffer));
          auto on_exit_release_buffer = on_exit ([mf_buffer] { release (mf_buffer); });

          BYTE *  buffer        = nullptr ;
          DWORD   buffer_length = 0       ;
          CHECK_HR ("Lock video buffer", mf_buffer->Lock (&buffer, nullptr, &buffer_length));
          auto on_exit_unlock_buffer = on_exit ([mf_buffer] { mf_buffer->Unlock (); });

          if ((i % 60) == 0)
          {
            std::printf ("Sending frame #%d\n", i);
          }

          if (buffer && buffer_length == 4*frame_width*frame_height)
          {
            sender.SendImage (buffer, frame_width, frame_height);
          }
        }
      }
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

