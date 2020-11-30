/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#ifndef cmSubdirDependsCommand_h
#define cmSubdirDependsCommand_h

#include "cmConfigure.h" // IWYU pragma: keep

#include <string>
#include <vector>

class cmExecutionStatus;

bool cmSubdirDependsCommand(std::vector<std::string> const& args,
                            cmExecutionStatus& status);

#endif
