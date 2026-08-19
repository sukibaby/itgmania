list(APPEND SMDATA_GLOBAL_FILES_SRC
            "GameLoop.cpp"
            "global.cpp"
            "SpecialFiles.cpp"
            "StepMania.cpp" # only functions which must run on every frame
            "StepManiaInit.cpp" # singleton management, filesystem & display bootstrap
            "StepManiaIO.cpp" # contents are handled by a worker thread
            "${SM_GENERATED_SRC_DIR}/verstub.cpp")

list(APPEND SMDATA_GLOBAL_FILES_HPP
            "GameLoop.h"
            "global.h"
            "PeriodicCaller.h"
            "ProductInfo.h" # TODO: Have this be auto-generated.
            "SpecialFiles.h"
            "StdString.h" # TODO: Remove the need for this file, transition to
                          # std::string.
            "StepMania.h" # TODO: Refactor into separate main project.
     )

source_group("Global Files"
             FILES
             ${SMDATA_GLOBAL_FILES_SRC}
             ${SMDATA_GLOBAL_FILES_HPP})
