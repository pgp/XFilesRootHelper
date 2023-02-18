#ifndef __RH_CPU_QUERY__
#define __RH_CPU_QUERY__

#if defined(_M_X64) \
       || defined(_M_AMD64) \
       || defined(__x86_64__) \
       || defined(__AMD64__) \
       || defined(__amd64__)
   #define RH_IS_X64
#endif

#if defined(_M_IX86) || defined(__i386__)
    #define RH_IS_X86
#endif

#if defined(RH_IS_X64) || defined(RH_IS_X86)
    #define RH_IS_X86_OR_64
#endif

////////////////////////////////////

// Web source: https://stackoverflow.com/questions/17758409/intrinsics-for-cpuid-like-informations
#if (defined(__GNUC__) || defined(__clang__)) && defined(RH_IS_X86_OR_64)
#include <cpuid.h>
#endif

// Web source: https://learn.microsoft.com/en-us/cpp/intrinsics/cpuid-cpuidex?view=msvc-170
#ifdef _MSC_VER
#include <intrin.h>
#endif

inline bool has_aes_hw_instructions() {
#ifdef __aarch64__ // ARMv8 architecture both supports AES and SHA hardware instructions
    return true;
#else
    // if CPU is x86 or x64, use assembly to detect aes hw instructions
    // copied from 7zip's CpuArch.h
    // see also https://stackoverflow.com/questions/152016/detecting-cpu-architecture-compile-time
    #if defined(RH_IS_X86_OR_64)
       // Web source: https://stackoverflow.com/questions/27193827/how-to-check-aes-ni-are-supported-by-cpu

        // Web source: https://stackoverflow.com/questions/28166565/detect-gcc-as-opposed-to-msvc-clang-with-macro
        // compiler is MSVC
        #ifdef _MSC_VER
            int cpuInfo[4] = { -1 };
            __cpuid(cpuInfo, 1);
            return (cpuInfo[2] & (1 << 25)) != 0;
        #elif (defined(__GNUC__) || defined(__clang__))
            __builtin_cpu_init();
            return __builtin_cpu_supports("aes") != 0;
        #else // unknown compiler, assume false
        return false;
        #endif
    #else
    return false; // assume false in all other cases (e.g. armv7)
    #endif
#endif
}

#endif /* __RH_CPU_QUERY__ */