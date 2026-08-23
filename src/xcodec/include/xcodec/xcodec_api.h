#ifndef XCODEC_API_H_
#define XCODEC_API_H_

#ifdef _WIN32
#ifdef XCODEC_EXPORTS
#define XCODEC_API __declspec(dllexport)
#else
#define XCODEC_API __declspec(dllimport)
#endif
#else
#define XCODEC_API
#endif

#endif  // XCODEC_API_H_
