# Deploy the installed app before CPack creates the archive. Keep source/build
# bundles untouched; both normal installation and packaging use this script.
if(NOT IS_DIRECTORY "${APP_BUNDLE}/Contents/MacOS")
  message(FATAL_ERROR "Missing application bundle: ${APP_BUNDLE}")
endif()
if(NOT EXISTS "${APP_BUNDLE}/Contents/MacOS/player")
  message(FATAL_ERROR "Missing bundled player backend: ${APP_BUNDLE}")
endif()
if(NOT EXISTS "${APP_BUNDLE}/Contents/Resources/licenses/COPYING")
  message(FATAL_ERROR "Install licenses before deploying and signing ${APP_BUNDLE}")
endif()

if(NOT EXISTS "${MACDEPLOYQT}")
  message(FATAL_ERROR "Missing Qt deployment tool: ${MACDEPLOYQT}")
endif()
get_filename_component(qt_bin_dir "${MACDEPLOYQT}" DIRECTORY)
execute_process(COMMAND "${qt_bin_dir}/qtpaths" --query QT_INSTALL_PLUGINS
  OUTPUT_VARIABLE qt_plugins_dir OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE query_result)
if(NOT query_result STREQUAL "0")
  message(FATAL_ERROR "Cannot locate Qt runtime plugins")
endif()
# Install only the platform and style plugins used by the Widgets tray interface.
set(required_plugin "${qt_plugins_dir}/platforms/libqcocoa.dylib")
if(NOT EXISTS "${required_plugin}")
  message(FATAL_ERROR "Missing Qt Cocoa plugin: ${required_plugin}")
endif()
file(REMOVE_RECURSE "${APP_BUNDLE}/Contents/PlugIns")
file(COPY "${required_plugin}" DESTINATION "${APP_BUNDLE}/Contents/PlugIns/platforms")
file(GLOB style_plugins "${qt_plugins_dir}/styles/*.dylib")
list(FILTER style_plugins EXCLUDE REGEX "_debug\\.dylib$")
if(style_plugins)
  file(COPY ${style_plugins} DESTINATION "${APP_BUNDLE}/Contents/PlugIns/styles")
endif()

include(BundleUtilities)
# Include the playback helper so its codec libraries and transitive dependencies
# are bundled alongside the GUI's Qt frameworks.
fixup_bundle("${APP_BUNDLE}" "${APP_BUNDLE}/Contents/MacOS/player" "")
execute_process(COMMAND "${MACDEPLOYQT}" "${APP_BUNDLE}" -no-plugins -always-overwrite -codesign=-
  OUTPUT_VARIABLE deploy_output ERROR_VARIABLE deploy_error RESULT_VARIABLE deploy_result)
if(NOT deploy_result STREQUAL "0" OR "${deploy_output}${deploy_error}" MATCHES "(^|\n)ERROR:")
  message(FATAL_ERROR "Qt deployment failed: ${deploy_output}${deploy_error}")
endif()
if(NOT EXISTS "${APP_BUNDLE}/Contents/Frameworks/QtCore.framework")
  message(FATAL_ERROR "Qt deployment did not bundle QtCore")
endif()

execute_process(
  COMMAND /usr/bin/codesign --force --deep --sign - "${APP_BUNDLE}"
  OUTPUT_VARIABLE signing_output
  ERROR_VARIABLE signing_error
  RESULT_VARIABLE signing_result
)
if(NOT signing_result STREQUAL "0")
  message(FATAL_ERROR "Ad-hoc signing failed: ${signing_output}${signing_error}")
endif()
execute_process(
  COMMAND /usr/bin/codesign --verify --deep --strict "${APP_BUNDLE}"
  OUTPUT_VARIABLE verification_output
  ERROR_VARIABLE verification_error
  RESULT_VARIABLE verification_result
)
if(NOT verification_result STREQUAL "0")
  message(FATAL_ERROR "Bundle signature verification failed: ${verification_output}${verification_error}")
endif()
