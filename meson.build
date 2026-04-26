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

# Core library sources
core_sources = [
  'src/BaseCameraClass.cpp',
  'src/CameraPropertiesEncoding.cpp',
  'src/CameraUtils.cpp',
  'src/ImageProcessingUtils.cpp',
  'src/ImagerPlugin.cpp',
  'src/PluginManager.cpp',
  'src/RobotProgramArguments.cpp',
  'src/StandardCameraProperties.cpp'
]

# Static library build
imager_core_lib = static_library('ImagerPluginCore',
  core_sources,
  include_directories: [inc_dir, internal_inc_dir],
  dependencies: [json_dep, concurrentqueue_dep]
)

# How consumers will see and link against this project
imager_core_dep = declare_dependency(
  include_directories: inc_dir,
  link_with: imager_core_lib,
  dependencies: [json_dep, concurrentqueue_dep]
)

# Register it for the wrap system (if included via subproject)
meson.override_dependency('imagerplugincore', imager_core_dep)
