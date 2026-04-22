set(qoi_generator_py "${CMAKE_SOURCE_DIR}/utils/qoi_packer.py")
set(qoi_data_file "${CMAKE_CURRENT_BINARY_DIR}/qoi.data")
set(qoi_resources_file "${CMAKE_BINARY_DIR}/src/gui/res/qoi_resources.gen")

# Icon source directory - overlay brass icons for Signature Oak
if(SIGNATURE_OAK)
  set(qoi_source_dir "${CMAKE_BINARY_DIR}/png_merged")
  set(png_standard_dir "${CMAKE_SOURCE_DIR}/src/gui/res/png")
  set(png_brass_dir "${CMAKE_SOURCE_DIR}/src/gui/res/png_brass")
  message(STATUS "Signature Oak: Overlaying brass icons")

  file(GLOB standard_pngs "${png_standard_dir}/*.png")
  file(GLOB brass_pngs "${png_brass_dir}/*.png")

  add_custom_command(
    OUTPUT "${qoi_source_dir}/.stamp"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${png_standard_dir}" "${qoi_source_dir}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${png_brass_dir}" "${qoi_source_dir}"
    COMMAND "${CMAKE_COMMAND}" -E touch "${qoi_source_dir}/.stamp"
    DEPENDS ${standard_pngs} ${brass_pngs}
    COMMENT "Merging brass icons for Signature Oak"
    VERBATIM
    )
  set(qoi_icon_deps "${qoi_source_dir}/.stamp")
else()
  set(qoi_source_dir "${CMAKE_SOURCE_DIR}/src/gui/res/png")
  set(qoi_icon_deps "")
endif()

if(PRINTER STREQUAL "MINI")
  # mini only loads images that are actually used, because it has small xflash
  set(qoi_used_files ${CMAKE_SOURCE_DIR}/src/gui/res/mini_used_imgs.txt)
  set(qou_used_files_arg -input_filter=${qoi_used_files})
endif()

add_custom_command(
  OUTPUT "${qoi_data_file}" "${qoi_resources_file}"
  COMMAND "${Python3_EXECUTABLE}" "${qoi_generator_py}" "${qoi_source_dir}" "${qoi_resources_file}"
          "${qoi_data_file}" ${qou_used_files_arg}
  DEPENDS ${qoi_icon_deps} "${qoi_generator_py}" "${qoi_used_files}"
  VERBATIM
  )

add_custom_target(qoi_generated_files DEPENDS "${qoi_data_file}" "${qoi_resources_file}")

add_dependencies(firmware qoi_generated_files)

target_include_directories(firmware PUBLIC "${CMAKE_BINARY_DIR}/src/gui/res")
