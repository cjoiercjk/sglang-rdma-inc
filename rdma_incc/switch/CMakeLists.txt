# the caller should set P4_NAME P4_PATH
project(RINCC_switch)

function(build_p4 P4_NAME P4_PATH)
    
    set(CMAKE_MODULE_PATH $ENV{SDE}/cmake)
    # include($ENV{SDE}/p4studio/CMakeLists.txt)
    add_subdirectory($ENV{SDE}/p4studio "${P4_NAME}_build") # here builds p4
    message("install pos ${CMAKE_INSTALL_PREFIX}")

    set_target_properties(bf-p4c PROPERTIES OUTPUT_NAME bf-p4c-${P4_NAME})
    set_target_properties(driver PROPERTIES OUTPUT_NAME driver-${P4_NAME})

    set(BFA_PATH ${CMAKE_CURRENT_BINARY_DIR}/${P4_NAME}_build/${P4_NAME}/tofino/pipe/${P4_NAME}.bfa)
    add_custom_target("show_stages_${P4_NAME}" ALL 
        COMMAND echo \"Take up `cat ${BFA_PATH} | grep -c -E \"stage.+ingress\"` ingress stages\"
        COMMAND echo \"Take up `cat ${BFA_PATH} | grep -c -E \"stage.+egress\"` egress stages\"
        DEPENDS ${P4_NAME})
endfunction()

build_p4(${P4_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/${P4_NAME}.p4)
