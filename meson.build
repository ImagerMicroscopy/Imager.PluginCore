project('ImagerPluginCore', 'cpp',
  version: '0.1.0',
  default_options: ['warning_level=3', 'cpp_std=c++20']
)

# Dependencies (these wrap files are located in subprojects/)
json_dep = dependency('nlohmann_json')
concurrentqueue_dep = dependency('concurrentqueue')

# Include directory (exposes 'ImagerPluginCore/...')
inc_dir = include_directories('include')
internal_inc_dir = include_directories('include/ImagerPluginCore')

toml_dep = dependency('tomlplusplus', static: true)

cmake = import('cmake')
# activate PIC code generation for static library generation on Linux. Does nothing on Windows.
serial_subproj = cmake.subproject('serial_cpp', cmake_options: ['-DCMAKE_POSITION_INDEPENDENT_CODE=ON'])
serial_dep = serial_subproj.dependency('serial_cpp')

# Cause the compile to fail if there are any switch statements
# that don't handle all enum cases. This exists to enforce support
# for postprocessing of new pixel format types.
cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'msvc'
  add_project_arguments('/we4062', language: 'cpp')
else
  add_project_arguments('-Werror=switch', language: 'cpp')
endif

# Core library sources
core_sources = [
  'src/BaseCameraClass.cpp',
  'src/CameraUtils.cpp',
  'src/ConfigManager.cpp',
  'src/ImageProcessingUtils.cpp',
  'src/CameraPropertiesEncoding.cpp',
  'src/PluginManager.cpp',
  'src/RobotProgramArguments.cpp',
  'src/SerialPort.cpp',
  'src/StandardCameraProperties.cpp',
]

api_source = files('src/ImagerPlugin.cpp')

# Static library build
imager_core_lib = static_library('ImagerPluginCore',
  core_sources,
  include_directories: [inc_dir, internal_inc_dir],
  dependencies: [json_dep, concurrentqueue_dep, toml_dep, serial_dep]
)

# How consumers will see and link against this project
imager_core_dep = declare_dependency(
  include_directories: inc_dir,
  link_with: imager_core_lib,
  dependencies: [json_dep, concurrentqueue_dep, toml_dep, serial_dep]
)

# Register it for the wrap system (if included via subproject)
meson.override_dependency('imagerplugincore', imager_core_dep)
