# ONNX Runtime 1.31.0 (MIT) for stem separation (docs/dev/spikes.md (a)): the official C API zip plus the
# WebGPU plugin execution provider from NuGet, downloaded once into build/_downloads/ like the soundfont and
# unpacked into <build>/_ort/. Nothing is checked in. The DLLs are copied next to the executables; the CUDA
# "acceleration pack" is a separate, optional user download (not a build input).
option(CC_DOWNLOAD_ORT "Download ONNX Runtime 1.31.0 + WebGPU EP into the build tree" ON)
set(CC_ORT_VERSION "1.31.0")
set(CC_ORT_WEBGPU_VERSION "0.4.0")
set(CC_ORT_ZIP "onnxruntime-win-x64-${CC_ORT_VERSION}.zip")
set(CC_ORT_URL "https://github.com/microsoft/onnxruntime/releases/download/v${CC_ORT_VERSION}/${CC_ORT_ZIP}")
set(CC_ORT_SHA256 "3958d8a44984160692c1b4ffd0de3cb48f8c39f9d34bb31e433db06e30b82195") # verified 2026-11-05, 82 887 349 bytes
set(CC_ORT_WEBGPU_NUPKG "microsoft.ml.onnxruntime.ep.webgpu.${CC_ORT_WEBGPU_VERSION}.nupkg")
set(CC_ORT_WEBGPU_URL "https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.EP.WebGpu/${CC_ORT_WEBGPU_VERSION}")
set(CC_ORT_WEBGPU_SHA256 "cc2e093a2eaa760a630d6c3299f015f1dd86885b9eb5877257135308f7c5e923") # verified 2026-11-05, 46 707 778 bytes

function(cc_download_once url file sha256 what)
    if(EXISTS "${file}")
        return()
    endif()
    message(STATUS "Downloading ${what} ...")
    file(DOWNLOAD "${url}" "${file}" EXPECTED_HASH SHA256=${sha256} STATUS _status)
    list(GET _status 0 _code)
    if(NOT _code EQUAL 0)
        file(REMOVE "${file}")
        message(FATAL_ERROR "${what} download failed: ${_status}. Set CC_DOWNLOAD_ORT=OFF to build without it.")
    endif()
endfunction()

set(CC_HAVE_ORT OFF)
if(CC_DOWNLOAD_ORT)
    set(_ort_root "${CMAKE_BINARY_DIR}/_ort")
    set(_ort_dir "${_ort_root}/onnxruntime-win-x64-${CC_ORT_VERSION}")
    set(_webgpu_dir "${_ort_root}/webgpu-${CC_ORT_WEBGPU_VERSION}")
    cc_download_once("${CC_ORT_URL}" "${CC_DOWNLOAD_DIR}/${CC_ORT_ZIP}" "${CC_ORT_SHA256}" "ONNX Runtime ${CC_ORT_VERSION} (79 MB, MIT)")
    cc_download_once("${CC_ORT_WEBGPU_URL}" "${CC_DOWNLOAD_DIR}/${CC_ORT_WEBGPU_NUPKG}" "${CC_ORT_WEBGPU_SHA256}" "ONNX Runtime WebGPU EP ${CC_ORT_WEBGPU_VERSION} (45 MB, MIT)")
    if(NOT EXISTS "${_ort_dir}/lib/onnxruntime.lib")
        file(ARCHIVE_EXTRACT INPUT "${CC_DOWNLOAD_DIR}/${CC_ORT_ZIP}" DESTINATION "${_ort_root}")
    endif()
    if(NOT EXISTS "${_webgpu_dir}/runtimes/win-x64/native/onnxruntime_providers_webgpu.dll")
        file(ARCHIVE_EXTRACT INPUT "${CC_DOWNLOAD_DIR}/${CC_ORT_WEBGPU_NUPKG}" DESTINATION "${_webgpu_dir}"
             PATTERNS "runtimes/win-x64/native/*" "LICENSE")
    endif()

    add_library(cc_onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(cc_onnxruntime PROPERTIES
        IMPORTED_LOCATION "${_ort_dir}/lib/onnxruntime.dll"
        IMPORTED_IMPLIB "${_ort_dir}/lib/onnxruntime.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${_ort_dir}/include")
    set_property(TARGET cc_onnxruntime PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_ort_dir}/include")
    # Next to every executable (they all live in the build root).
    file(COPY
        "${_ort_dir}/lib/onnxruntime.dll"
        "${_ort_dir}/lib/onnxruntime_providers_shared.dll"
        "${_webgpu_dir}/runtimes/win-x64/native/onnxruntime_providers_webgpu.dll"
        "${_webgpu_dir}/runtimes/win-x64/native/dxcompiler.dll"
        "${_webgpu_dir}/runtimes/win-x64/native/dxil.dll"
        DESTINATION "${CMAKE_BINARY_DIR}")
    file(COPY "${_ort_dir}/LICENSE" DESTINATION "${CMAKE_BINARY_DIR}/licenses/onnxruntime")
    file(COPY "${_webgpu_dir}/LICENSE" DESTINATION "${CMAKE_BINARY_DIR}/licenses/onnxruntime-webgpu")
    set(CC_HAVE_ORT ON)
endif()
