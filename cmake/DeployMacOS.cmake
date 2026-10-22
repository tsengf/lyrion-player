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

include(BundleUtilities)
# Copies codec dylibs and their transitive dependencies, rewrites install names,
# and verifies that non-system dependencies resolve inside the installed bundle.
fixup_bundle("${APP_BUNDLE}" "${APP_BUNDLE}/Contents/MacOS/player" "")

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
