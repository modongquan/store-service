if(NOT "/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CMakeTests" MATCHES "^/")
  set(slash /)
endif()
set(url "file://${slash}/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CMakeTests/FileDownloadInput.png")
set(dir "/mnt/teamway/store-service/tools/cmake-3.18.2/Tests/CMakeTests/downloads")

file(DOWNLOAD
  ${url}
  ${dir}/file3.png
  TIMEOUT 2
  STATUS status
  EXPECTED_HASH SHA1=5555555555555555555555555555555555555555
  )
