# CMake generated Testfile for 
# Source directory: /mnt/teamway/store-service/tools/cmake-3.18.2/Utilities/cmcurl
# Build directory: /mnt/teamway/store-service/tools/cmake-3.18.2/Utilities/cmcurl
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test(curl "curltest" "http://open.cdash.org/user.php")
set_tests_properties(curl PROPERTIES  _BACKTRACE_TRIPLES "/mnt/teamway/store-service/tools/cmake-3.18.2/Utilities/cmcurl/CMakeLists.txt;1425;add_test;/mnt/teamway/store-service/tools/cmake-3.18.2/Utilities/cmcurl/CMakeLists.txt;0;")
subdirs("lib")
