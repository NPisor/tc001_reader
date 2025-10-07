#pragma once
#if defined(_WIN32) || defined(_WIN64)
  #if defined(TC001_BUILD_DLL)
    #define TC001_API __declspec(dllexport)
  #else
    #define TC001_API __declspec(dllimport)
  #endif
#else
  #define TC001_API __attribute__((visibility("default")))
#endif
